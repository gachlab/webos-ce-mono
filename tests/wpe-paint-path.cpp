// WPE frame into the buffer shape WebAppMgr paints through:
// RemoteWindowDataSoftwareQt's QImage(ARGB32_Premultiplied) + QPainter.
//
// Mirrors WindowedWebApp::paint's call shape:
//   ctxt->fillRect(...);
//   mainFrame()->render(ctxt, ...);
// with WPE's latest buffer standing in for grabView().

#include "wpe_webcontent.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QFile>
#include <QUrl>

#include <cstdio>

static int report(const char* what, bool ok, const QString& detail)
{
    printf("%-12s : %-4s %s\n", what, ok ? "yes" : "NO", qPrintable(detail));
    return ok ? 0 : 1;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    int failures = 0;

    QTemporaryDir dir;
    {
        QFile page(dir.filePath("page.html"));
        if (!page.open(QIODevice::WriteOnly)) {
            printf("FAIL: could not write page.html\n");
            return 1;
        }
        page.write(R"HTML(<!doctype html>
<html><body style="margin:0; background:#ff00c8; width:200px; height:200px"></body></html>
)HTML");
    }

    wpe_webcontent::HeadlessView view(200, 200);
    view.load(QUrl::fromLocalFile(dir.filePath("page.html")));
    if (!view.waitUntilLoaded(30000)) {
        printf("FAIL: page did not load\n");
        return 1;
    }
    if (!view.waitUntil([&] { return view.hasFrame(); }, 10000)) {
        printf("FAIL: no frame within 10 s\n");
        return 1;
    }

    // Same format RemoteWindowDataSoftwareQt wraps around the IPC shm.
    QImage surface(200, 200, QImage::Format_ARGB32_Premultiplied);
    surface.fill(Qt::transparent);
    {
        QPainter ctxt(&surface);
        ctxt.fillRect(surface.rect(), Qt::transparent);
        // Stand-in for QWebFrame::render → painter->drawPixmap(...).
        ctxt.drawImage(0, 0, view.frame());
    }

    const QColor pixel = surface.pixelColor(100, 100);
    failures += report("paint-blit", pixel == QColor("#ff00c8"),
                       "pixel in ARGB32_Premultiplied surface: " + pixel.name());

    printf("%s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
