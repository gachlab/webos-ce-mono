// Many-card present A/B (#79): N QtWebEngine views × local browser-like page.
//
// Same shape as the #81 card-load harness, but the axis is present path:
//
//   QT_QPA_PLATFORM=offscreen ./engine-card-load grab 25
//   QT_QPA_PLATFORM=offscreen ./engine-card-load direct 25
//
// Proof: every card loads, title is card-i, and a paint is non-black.
// Also prints median present ms across the N paints (the #79 bar).

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebFrame>
#include <QWebPage>

#include <algorithm>
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
  .hero { width: 100%; height: 120px; background: linear-gradient(135deg,#ff00c8,#0033aa); }
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
    tempor incididunt ut labore et dolore magna aliqua.</p>
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

static double medianMs(std::vector<double> samples)
{
    if (samples.empty())
        return 0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

struct Stats {
    int ok = 0;
    int fail = 0;
    qint64 wallMs = 0;
    long peakTreeRssKb = 0;
    int endProcCount = 0;
    double medianPresentMs = 0;
};

static void samplePeak(long* peak)
{
    const long now = treeRssKb(getpid());
    if (now > *peak)
        *peak = now;
}

static Stats run(int n, int width, int height, const QString& pagePath)
{
    Stats s;
    std::vector<std::unique_ptr<QWebPage>> pages;
    pages.reserve(static_cast<size_t>(n));
    std::vector<bool> loaded(static_cast<size_t>(n), false);
    std::vector<double> presentMs;

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
        QElapsedTimer present;
        present.start();
        {
            QPainter painter(&image);
            page->mainFrame()->render(&painter, QWebFrame::ContentsLayer,
                                      QRegion(0, 0, width, height));
        }
        presentMs.push_back(present.nsecsElapsed() / 1e6);
        const QColor px = image.pixelColor(width / 2, 8);
        const bool painted = px.blue() > 100 || px.red() > 100;
        if (allLoaded && loaded[static_cast<size_t>(i)] && title == want && painted)
            ++s.ok;
        else
            ++s.fail;
        samplePeak(&s.peakTreeRssKb);
    }

    s.wallMs = wall.elapsed();
    s.medianPresentMs = medianMs(presentMs);
    samplePeak(&s.peakTreeRssKb);
    {
        std::vector<pid_t> pids;
        collectDescendants(getpid(), &pids);
        s.endProcCount = static_cast<int>(pids.size());
    }
    return s;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    const char* mode = argc > 1 ? argv[1] : "direct";
    const int n = argc > 2 ? std::atoi(argv[2]) : 25;
    if (n < 1 || (std::strcmp(mode, "grab") != 0 && std::strcmp(mode, "direct") != 0)) {
        std::printf("usage: %s grab|direct [n]\n", argv[0]);
        return 2;
    }

    if (std::strcmp(mode, "grab") == 0)
        qputenv("WEBOS_GRAB_PRESENT", "1");
    else
        qputenv("WEBOS_GRAB_PRESENT", "0");

    QApplication app(argc, argv);

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

    const Stats s = run(n, 360, 480, pagePath);
    std::printf("present=%s n=%d ok=%d fail=%d wall_ms=%lld peak_tree_rss_mb=%.1f procs=%d median_present_ms=%.3f\n",
                mode, n, s.ok, s.fail, static_cast<long long>(s.wallMs),
                s.peakTreeRssKb / 1024.0, s.endProcCount, s.medianPresentMs);
    return s.fail == 0 && s.ok == n ? 0 : 1;
}
