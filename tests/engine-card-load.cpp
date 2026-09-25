// Many-card load A/B: N views each open a local "browser" page (#81).
//
// One engine per process so VmHWM / tree RSS are not polluted by the other.
//   QT_QPA_PLATFORM=offscreen ./engine-card-load wpe 150
//   QT_QPA_PLATFORM=offscreen ./engine-card-load qtwebengine 150
//
// Proof is not silence: every card must load, paint once, and expose a
// per-card title. Peak RSS is the sum of this process tree (renderers included).

#include "wpe_webcontent.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QPainter>
#include <QFile>
#include <QTemporaryDir>
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

static const char kBrowserPage[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<style>
  body { margin: 0; font: 16px/1.4 sans-serif; background: #f4f4f4; color: #222; }
  header { background: #1a73e8; color: #fff; padding: 12px 16px; }
  article { margin: 12px; padding: 16px; background: #fff; border-radius: 8px; }
  .chip { display: inline-block; margin: 4px; padding: 4px 10px; background: #e8f0fe; border-radius: 12px; }
  img.hero { width: 100%; height: 120px; object-fit: cover; background: linear-gradient(135deg,#ff00c8,#0033aa); }
</style>
<script>
  document.title = "card-" + (location.search.match(/n=(\d+)/) || [,0])[1];
</script>
</head>
<body>
  <header>Browser card <span id="n"></span></header>
  <article>
    <div class="hero" role="img" aria-label="hero"></div>
    <h1>Example site</h1>
    <p>Enough DOM that a real card pays for layout and paint — not an empty body.</p>
    <div>
      <span class="chip">news</span><span class="chip">sports</span>
      <span class="chip">weather</span><span class="chip">finance</span>
    </div>
    <p>Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod
    tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam.</p>
  </article>
  <script>document.getElementById("n").textContent = document.title;</script>
</body></html>
)HTML";

static bool waitFor(const std::function<bool()>& done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
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

// Collect every PID whose /proc/PID/stat ppid chain reaches root (this process).
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
        // pid (comm) state ppid
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

static Stats runWpe(int n, int width, int height, const QString& pagePath)
{
    Stats s;
    std::vector<std::unique_ptr<wpe_webcontent::HeadlessView>> views;
    views.reserve(static_cast<size_t>(n));
    std::vector<bool> loaded(static_cast<size_t>(n), false);
    std::vector<bool> framed(static_cast<size_t>(n), false);

    QElapsedTimer wall;
    wall.start();

    for (int i = 0; i < n; ++i) {
        auto view = std::make_unique<wpe_webcontent::HeadlessView>(width, height);
        const int idx = i;
        view->setLoadFinishedCallback([&, idx](bool ok) {
            if (ok)
                loaded[static_cast<size_t>(idx)] = true;
        });
        view->setFrameCallback([&, idx]() { framed[static_cast<size_t>(idx)] = true; });
        view->load(QUrl(QUrl::fromLocalFile(pagePath).toString()
                            + "?n=" + QString::number(i)));
        views.push_back(std::move(view));
        samplePeak(&s.peakTreeRssKb);
    }

    const bool allDone = waitFor([&] {
        samplePeak(&s.peakTreeRssKb);
        for (int i = 0; i < n; ++i) {
            if (!loaded[static_cast<size_t>(i)] || !framed[static_cast<size_t>(i)])
                return false;
        }
        return true;
    }, 180000);

    for (int i = 0; i < n; ++i) {
        const QString title = views[static_cast<size_t>(i)]->title();
        const QString want = QStringLiteral("card-%1").arg(i);
        const bool painted = !views[static_cast<size_t>(i)]->frame().isNull();
        if (allDone && loaded[static_cast<size_t>(i)] && framed[static_cast<size_t>(i)]
            && title == want && painted)
            ++s.ok;
        else
            ++s.fail;
    }

    s.wallMs = wall.elapsed();
    samplePeak(&s.peakTreeRssKb);
    s.endTreeRssKb = treeRssKb(getpid());
    {
        std::vector<pid_t> pids;
        collectDescendants(getpid(), &pids);
        s.endProcCount = static_cast<int>(pids.size());
    }
    return s;
}

static Stats runQt(int n, int width, int height, const QString& pagePath)
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
        page->mainFrame()->load(QUrl(QUrl::fromLocalFile(pagePath).toString()
                                     + "?n=" + QString::number(i)));
        pages.push_back(std::move(page));
        samplePeak(&s.peakTreeRssKb);
    }

    const bool allLoaded = waitFor([&] {
        samplePeak(&s.peakTreeRssKb);
        for (int i = 0; i < n; ++i) {
            if (!loaded[static_cast<size_t>(i)])
                return false;
        }
        return true;
    }, 180000);

    for (int i = 0; i < n; ++i) {
        auto* page = pages[static_cast<size_t>(i)].get();
        const QString title = page->mainFrame()->title();
        const QString want = QStringLiteral("card-%1").arg(i);
        QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);
        {
            QPainter painter(&image);
            page->mainFrame()->render(&painter, QWebFrame::ContentsLayer,
                                      QRegion(0, 0, width, height));
        }
        // Non-black proof of paint (hero gradient / header blue).
        const QColor px = image.pixelColor(width / 2, 8);
        const bool painted = px.blue() > 100 || px.red() > 100;
        if (allLoaded && loaded[static_cast<size_t>(i)] && title == want && painted)
            ++s.ok;
        else
            ++s.fail;
        samplePeak(&s.peakTreeRssKb);
    }

    s.wallMs = wall.elapsed();
    samplePeak(&s.peakTreeRssKb);
    s.endTreeRssKb = treeRssKb(getpid());
    {
        std::vector<pid_t> pids;
        collectDescendants(getpid(), &pids);
        s.endProcCount = static_cast<int>(pids.size());
    }
    return s;
}

static void printStats(const char* engine, int n, const Stats& s)
{
    std::printf("engine=%s n=%d ok=%d fail=%d wall_ms=%lld peak_tree_rss_mb=%.1f end_tree_rss_mb=%.1f procs=%d\n",
                engine, n, s.ok, s.fail, static_cast<long long>(s.wallMs),
                s.peakTreeRssKb / 1024.0, s.endTreeRssKb / 1024.0, s.endProcCount);
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);

    const char* engine = argc > 1 ? argv[1] : "wpe";
    const int n = argc > 2 ? std::atoi(argv[2]) : 150;
    const int width = 360;
    const int height = 480;
    if (n < 1) {
        std::printf("usage: %s wpe|qtwebengine [n]\n", argv[0]);
        return 2;
    }

    QTemporaryDir dir;
    const QString pagePath = dir.filePath("browser.html");
    {
        QFile f(pagePath);
        if (!f.open(QIODevice::WriteOnly)) {
            std::printf("FAIL: write page\n");
            return 1;
        }
        f.write(kBrowserPage);
    }

    Stats s;
    if (std::strcmp(engine, "wpe") == 0)
        s = runWpe(n, width, height, pagePath);
    else if (std::strcmp(engine, "qtwebengine") == 0)
        s = runQt(n, width, height, pagePath);
    else {
        std::printf("unknown engine %s\n", engine);
        return 2;
    }

    printStats(engine, n, s);
    return s.fail == 0 && s.ok == n ? 0 : 1;
}
