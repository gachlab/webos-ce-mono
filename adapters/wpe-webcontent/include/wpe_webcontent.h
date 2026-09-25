// Minimal WPE headless view: load a page, read frames as QImage, deliver clicks.
// Enough for the WebAppMgr paint contract (QPainter blit into ARGB32 shm).

#pragma once

#include <QImage>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <functional>
#include <memory>

namespace wpe_webcontent {

class HeadlessView {
public:
    HeadlessView(int width, int height);
    ~HeadlessView();

    HeadlessView(const HeadlessView&) = delete;
    HeadlessView& operator=(const HeadlessView&) = delete;

    // DocumentCreation-style injection. Replaces any previous document-start
    // script from setDocumentStartScript / injectAtDocumentStart.
    void injectAtDocumentStart(const QString& source);
    void clearUserScripts();
    // Full DocumentCreation payload (bridge core + collected assignments).
    void setDocumentStartScript(const QString& source);
    // Call after changing user scripts so the next load sees them.
    void rebindUserContent();

    // Shared WebKit context URI-scheme handler. Called with the request URI;
    // return body bytes and content-type. Registered once for "webos-bridge".
    using SchemeHandler = std::function<QByteArray(const QUrl& url, QString* contentType)>;
    static void setBridgeSchemeHandler(SchemeHandler handler);

    void load(const QUrl& url);
    void loadHtml(const QString& html, const QUrl& baseUrl = QUrl());
    bool waitUntilLoaded(int timeoutMs = 30000);
    bool waitUntil(const std::function<bool()>& done, int timeoutMs);

    QString title() const;
    // Deep-copied latest frame (BGRA → QImage::Format_ARGB32). Null until the
    // first buffer-rendered.
    QImage frame() const;
    bool hasFrame() const;

    // Drop the cached frame so waitUntil(hasFrame) observes the next present.
    void clearFrame();

    void click(double x, double y);
    // Precise pixel scroll (touchpad-style). Negative deltaY increases scrollY
    // on WPE 2.54 headless (measured).
    void scroll(double x, double y, double deltaX, double deltaY);

    void resize(int width, int height);

    // Optional notifications (same thread; caller must pump GLib or use the
    // Qt timer pump in wpewebkit-compat).
    void setLoadFinishedCallback(std::function<void(bool ok)> cb);
    void setFrameCallback(std::function<void()> cb);
    void setTitleCallback(std::function<void(const QString& title)> cb);

    // Runs script in the page and waits for the result (nested GLib loop).
    QVariant evaluateJavaScript(const QString& script);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace wpe_webcontent
