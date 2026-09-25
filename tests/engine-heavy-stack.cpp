// Product-shaped heavy stack A/B (#81): N pages in ONE process loading real
// HTTPS sites (YouTube, etc.) — closer to WebAppMgr's card stack than N
// isolated headless browsers.
//
//   QT_QPA_PLATFORM=offscreen ./engine-heavy-stack wpe 8
//   QT_QPA_PLATFORM=offscreen ./engine-heavy-stack qtwebengine 8
//
// Proof: each page reaches loadFinished and a non-empty title (or painted
// non-blank frame). Tree RSS includes renderers.

#include "wpe_webcontent.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QWebFrame>
#include <QWebPage>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

static const char* kUrls[] = {
    "https://www.youtube.com/",
    "https://www.youtube.com/watch?v=jNQXAC9IVRw",
    "https://www.cnn.com/",
    "https://www.reddit.com/",
    "https://www.nytimes.com/",
    "https://www.twitch.tv/",
    "https://www.wikipedia.org/wiki/WebOS",
    "https://github.com/",
    "https://www.bbc.com/",
    "https://www.amazon.com/",
    "https://stackoverflow.com/",
    "https://www.instagram.com/",
    "https://maps.google.com/",
    "https://www.facebook.com/",
    "https://www.netflix.com/",
    "https://www.spotify.com/",
};
static const int kUrlCount = sizeof(kUrls) / sizeof(kUrls[0]);

static bool waitFor(const std::function<bool()>& done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

static long readVmRssKb(pid_t pid)
{
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%d/status", static_cast<int>(pid));
    FILE* f = std::fopen(path, "r");
    if (!f)
        return 0;
    char line[256];
    long kb = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "VmRSS:", 6) == 0) {
            std::sscanf(line + 6, "%ld", &kb);
            break;
        }
    }
    std::fclose(f);
    return kb;
}

static void collectDescendants(pid_t root, std::vector<pid_t>* out)
{
    DIR* d = opendir("/proc");
    if (!d)
        return;
    std::vector<pid_t> all;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9')
            continue;
        all.push_back(static_cast<pid_t>(std::atoi(e->d_name)));
    }
    closedir(d);
    auto ppidOf = [](pid_t pid) -> pid_t {
        char path[64];
        std::snprintf(path, sizeof(path), "/proc/%d/stat", static_cast<int>(pid));
        FILE* f = std::fopen(path, "r");
        if (!f)
            return -1;
        int p = -1;
        if (std::fscanf(f, "%*d %*s %*c %d", &p) != 1)
            p = -1;
        std::fclose(f);
        return static_cast<pid_t>(p);
    };
    out->clear();
    out->push_back(root);
    bool grew = true;
    while (grew) {
        grew = false;
        for (pid_t cand : all) {
            bool known = false;
            for (pid_t k : *out) {
                if (k == cand) {
                    known = true;
                    break;
                }
            }
            if (known)
                continue;
            const pid_t pp = ppidOf(cand);
            for (pid_t k : *out) {
                if (pp == k) {
                    out->push_back(cand);
                    grew = true;
                    break;
                }
            }
        }
    }
}

static long treeRssKb(pid_t root)
{
    std::vector<pid_t> pids;
    collectDescendants(root, &pids);
    long sum = 0;
    for (pid_t p : pids)
        sum += readVmRssKb(p);
    return sum;
}

struct Stats {
    int ok = 0;
    int fail = 0;
    qint64 wallMs = 0;
    long peakTreeRssKb = 0;
    long endTreeRssKb = 0;
    int endProcCount = 0;
};

static void samplePeak(long* peak)
{
    const long now = treeRssKb(getpid());
    if (now > *peak)
        *peak = now;
}

static Stats runWpe(int n, int width, int height)
{
    Stats s;
    std::vector<std::unique_ptr<wpe_webcontent::HeadlessView>> views;
    views.reserve(static_cast<size_t>(n));
    std::vector<bool> loaded(static_cast<size_t>(n), false);

    QElapsedTimer wall;
    wall.start();
    for (int i = 0; i < n; ++i) {
        auto view = std::make_unique<wpe_webcontent::HeadlessView>(width, height);
        const int idx = i;
        view->setLoadFinishedCallback([&, idx](bool ok) {
            if (ok)
                loaded[static_cast<size_t>(idx)] = true;
        });
        view->load(QUrl(QString::fromUtf8(kUrls[i % kUrlCount])));
        views.push_back(std::move(view));
        samplePeak(&s.peakTreeRssKb);
        std::printf("  wpe launched %d %s\n", i, kUrls[i % kUrlCount]);
    }

    waitFor([&] {
        samplePeak(&s.peakTreeRssKb);
        for (int i = 0; i < n; ++i) {
            if (!loaded[static_cast<size_t>(i)])
                return false;
        }
        return true;
    }, 300000);

    // Extra settle for media-heavy pages after loadFinished.
    {
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 20000) {
            samplePeak(&s.peakTreeRssKb);
            QEventLoop loop;
            QTimer::singleShot(200, &loop, &QEventLoop::quit);
            loop.exec();
        }
    }

    for (int i = 0; i < n; ++i) {
        const QString title = views[static_cast<size_t>(i)]->title();
        const bool painted = views[static_cast<size_t>(i)]->hasFrame()
            && !views[static_cast<size_t>(i)]->frame().isNull();
        const bool ok = loaded[static_cast<size_t>(i)] && painted && !title.isEmpty();
        std::printf("  wpe card %d title=%s painted=%d\n", i, qPrintable(title.left(60)), painted);
        if (ok)
            ++s.ok;
        else
            ++s.fail;
    }
    s.wallMs = wall.elapsed();
    samplePeak(&s.peakTreeRssKb);
    s.endTreeRssKb = treeRssKb(getpid());
    std::vector<pid_t> pids;
    collectDescendants(getpid(), &pids);
    s.endProcCount = static_cast<int>(pids.size());
    return s;
}

static Stats runQt(int n, int width, int height)
{
    Stats s;
    std::vector<std::unique_ptr<QWebPage>> pages;
    pages.reserve(static_cast<size_t>(n));
    std::vector<bool> loaded(static_cast<size_t>(n), false);

    QElapsedTimer wall;
    wall.start();
    for (int i = 0; i < n; ++i) {
        auto page = std::make_unique<QWebPage>();
        page->setViewportSize(QSize(width, height));
        const int idx = i;
        QObject::connect(page.get(), &QWebPage::loadFinished, page.get(),
                         [&, idx](bool ok) {
                             if (ok)
                                 loaded[static_cast<size_t>(idx)] = true;
                         });
        page->mainFrame()->load(QUrl(QString::fromUtf8(kUrls[i % kUrlCount])));
        pages.push_back(std::move(page));
        samplePeak(&s.peakTreeRssKb);
        std::printf("  qt launched %d %s\n", i, kUrls[i % kUrlCount]);
    }

    waitFor([&] {
        samplePeak(&s.peakTreeRssKb);
        for (int i = 0; i < n; ++i) {
            if (!loaded[static_cast<size_t>(i)])
                return false;
        }
        return true;
    }, 300000);

    {
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 20000) {
            samplePeak(&s.peakTreeRssKb);
            QEventLoop loop;
            QTimer::singleShot(200, &loop, &QEventLoop::quit);
            loop.exec();
        }
    }

    for (int i = 0; i < n; ++i) {
        auto* page = pages[static_cast<size_t>(i)].get();
        const QString title = page->mainFrame()->title();
        QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);
        {
            QPainter painter(&image);
            page->mainFrame()->render(&painter, QWebFrame::ContentsLayer,
                                      QRegion(0, 0, width, height));
        }
        int nonzero = 0;
        for (int y = 0; y < height; y += 8) {
            for (int x = 0; x < width; x += 8) {
                if (image.pixelColor(x, y) != QColor(Qt::black))
                    ++nonzero;
            }
        }
        const bool painted = nonzero > 10;
        const bool ok = loaded[static_cast<size_t>(i)] && painted && !title.isEmpty();
        std::printf("  qt card %d title=%s painted=%d\n", i, qPrintable(title.left(60)), painted);
        if (ok)
            ++s.ok;
        else
            ++s.fail;
        samplePeak(&s.peakTreeRssKb);
    }
    s.wallMs = wall.elapsed();
    samplePeak(&s.peakTreeRssKb);
    s.endTreeRssKb = treeRssKb(getpid());
    std::vector<pid_t> pids;
    collectDescendants(getpid(), &pids);
    s.endProcCount = static_cast<int>(pids.size());
    return s;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    const char* engine = argc > 1 ? argv[1] : "wpe";
    const int n = argc > 2 ? std::atoi(argv[2]) : 8;
    if (n < 1) {
        std::printf("usage: %s wpe|qtwebengine [n]\n", argv[0]);
        return 2;
    }
    const int width = 360;
    const int height = 640;

    Stats s;
    if (std::strcmp(engine, "wpe") == 0)
        s = runWpe(n, width, height);
    else if (std::strcmp(engine, "qtwebengine") == 0)
        s = runQt(n, width, height);
    else {
        std::printf("unknown engine\n");
        return 2;
    }

    std::printf("engine=%s n=%d ok=%d fail=%d wall_ms=%lld peak_tree_rss_mb=%.1f end_tree_rss_mb=%.1f procs=%d\n",
                engine, n, s.ok, s.fail, static_cast<long long>(s.wallMs),
                s.peakTreeRssKb / 1024.0, s.endTreeRssKb / 1024.0, s.endProcCount);
    return s.fail == 0 && s.ok == n ? 0 : 1;
}
