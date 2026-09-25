// GLib before Qt: Qt's `signals` macro breaks gio headers otherwise.
#include <wpe/webkit.h>
#include <wpe/headless/wpe-headless.h>
#include <jsc/jsc.h>

#include "wpe_webcontent.h"

#include <QByteArray>

#include <cstring>

namespace wpe_webcontent {

namespace {

WebKitWebContext* sharedContext()
{
    static WebKitWebContext* context = nullptr;
    if (context)
        return context;
    context = webkit_web_context_new();
    WebKitSecurityManager* security = webkit_web_context_get_security_manager(context);
    webkit_security_manager_register_uri_scheme_as_local(security, "webos-bridge");
    webkit_security_manager_register_uri_scheme_as_secure(security, "webos-bridge");
    webkit_security_manager_register_uri_scheme_as_cors_enabled(security, "webos-bridge");
    return context;
}

HeadlessView::SchemeHandler& bridgeHandlerSlot()
{
    static HeadlessView::SchemeHandler handler;
    return handler;
}

void onBridgeScheme(WebKitURISchemeRequest* request, gpointer)
{
    const HeadlessView::SchemeHandler handler = bridgeHandlerSlot();
    QString contentType = QStringLiteral("application/json");
    QByteArray body;
    if (handler) {
        const char* uri = webkit_uri_scheme_request_get_uri(request);
        body = handler(QUrl(QString::fromUtf8(uri)), &contentType);
    } else {
        body = QByteArrayLiteral("{\"e\":\"no bridge handler\"}");
    }
    GInputStream* stream = g_memory_input_stream_new_from_data(
        g_memdup2(body.constData(), static_cast<gsize>(body.size())),
        static_cast<gssize>(body.size()), g_free);
    webkit_uri_scheme_request_finish(request, stream, body.size(),
                                     contentType.toUtf8().constData());
    g_object_unref(stream);
}

void ensureBridgeSchemeRegistered()
{
    static bool registered = false;
    if (registered)
        return;
    webkit_web_context_register_uri_scheme(sharedContext(), "webos-bridge",
                                           onBridgeScheme, nullptr, nullptr);
    registered = true;
}

} // namespace

struct HeadlessView::Impl {
    WPEDisplay* display = nullptr;
    WebKitWebContext* context = nullptr;
    WebKitUserContentManager* ucm = nullptr;
    WebKitSettings* settings = nullptr;
    WebKitWebView* webView = nullptr;
    WPEView* view = nullptr;
    bool loaded = false;
    QImage latest;
    int width = 0;
    int height = 0;
    std::function<void(bool)> onLoadFinished;
    std::function<void()> onFrame;
    std::function<void(const QString&)> onTitle;

    static void onLoadChanged(WebKitWebView*, WebKitLoadEvent load_event, gpointer user_data)
    {
        auto* impl = static_cast<Impl*>(user_data);
        if (load_event != WEBKIT_LOAD_FINISHED)
            return;
        impl->loaded = true;
        if (impl->onLoadFinished)
            impl->onLoadFinished(true);
    }

    static void onTitleChanged(WebKitWebView* webView, GParamSpec*, gpointer user_data)
    {
        auto* impl = static_cast<Impl*>(user_data);
        if (!impl->onTitle)
            return;
        const char* t = webkit_web_view_get_title(webView);
        impl->onTitle(t ? QString::fromUtf8(t) : QString());
    }

    static void onBufferRendered(WPEView*, WPEBuffer* buffer, gpointer user_data)
    {
        auto* impl = static_cast<Impl*>(user_data);
        GError* error = nullptr;
        GBytes* bytes = wpe_buffer_import_to_pixels(buffer, &error);
        if (!bytes) {
            if (error)
                g_error_free(error);
            return;
        }

        const int width = wpe_buffer_get_width(buffer);
        const int height = wpe_buffer_get_height(buffer);
        gsize len = 0;
        const auto* px = static_cast<const guint8*>(g_bytes_get_data(bytes, &len));
        if (!px || width < 1 || height < 1 || len < static_cast<gsize>(width * height * 4))
            return;

        QImage frame(width, height, QImage::Format_ARGB32);
        const int stride = width * 4;
        for (int y = 0; y < height; ++y) {
            memcpy(frame.scanLine(y), px + static_cast<gsize>(y) * static_cast<gsize>(stride),
                   static_cast<size_t>(stride));
        }
        impl->latest = frame;
        if (impl->onFrame)
            impl->onFrame();
    }
};

HeadlessView::HeadlessView(int width, int height)
    : m(std::make_unique<Impl>())
{
    m->width = width;
    m->height = height;

    GError* error = nullptr;
    m->display = wpe_display_headless_new();
    if (!wpe_display_connect(m->display, &error)) {
        if (error)
            g_error_free(error);
        return;
    }

    m->context = sharedContext();
    ensureBridgeSchemeRegistered();

    m->ucm = webkit_user_content_manager_new();
    m->settings = webkit_settings_new();
    webkit_settings_set_allow_file_access_from_file_urls(m->settings, TRUE);
    webkit_settings_set_allow_universal_access_from_file_urls(m->settings, TRUE);

    m->webView = WEBKIT_WEB_VIEW(g_object_new(
        WEBKIT_TYPE_WEB_VIEW,
        "display", m->display,
        "web-context", m->context,
        "user-content-manager", m->ucm,
        "settings", m->settings,
        nullptr));

    m->view = webkit_web_view_get_wpe_view(m->webView);
    wpe_view_resized(m->view, width, height);
    wpe_view_map(m->view);
    wpe_view_focus_in(m->view);

    g_signal_connect(m->view, "buffer-rendered", G_CALLBACK(Impl::onBufferRendered), m.get());
    g_signal_connect(m->webView, "load-changed", G_CALLBACK(Impl::onLoadChanged), m.get());
    g_signal_connect(m->webView, "notify::title", G_CALLBACK(Impl::onTitleChanged), m.get());
}

HeadlessView::~HeadlessView()
{
    if (m->webView)
        g_object_unref(m->webView);
    if (m->settings)
        g_object_unref(m->settings);
    if (m->ucm)
        g_object_unref(m->ucm);
    // Shared context is never released.
    if (m->display)
        g_object_unref(m->display);
}

void HeadlessView::setBridgeSchemeHandler(SchemeHandler handler)
{
    bridgeHandlerSlot() = std::move(handler);
    ensureBridgeSchemeRegistered();
}

void HeadlessView::clearUserScripts()
{
    webkit_user_content_manager_remove_all_scripts(m->ucm);
}

void HeadlessView::setDocumentStartScript(const QString& source)
{
    clearUserScripts();
    injectAtDocumentStart(source);
}

// Recreate the web view so newly added user scripts bind before navigation.
// WPE applies DocumentStart scripts registered on the UCM at view construction
// time for the next load; adding them after the view exists was a no-op here
// (measured: __webosBridge stayed undefined).
void HeadlessView::rebindUserContent()
{
    if (!m->webView)
        return;
    g_signal_handlers_disconnect_by_data(m->view, m.get());
    g_signal_handlers_disconnect_by_data(m->webView, m.get());
    g_object_unref(m->webView);

    m->webView = WEBKIT_WEB_VIEW(g_object_new(
        WEBKIT_TYPE_WEB_VIEW,
        "display", m->display,
        "web-context", m->context,
        "user-content-manager", m->ucm,
        "settings", m->settings,
        nullptr));
    m->view = webkit_web_view_get_wpe_view(m->webView);
    wpe_view_resized(m->view, m->width, m->height);
    wpe_view_map(m->view);
    wpe_view_focus_in(m->view);
    g_signal_connect(m->view, "buffer-rendered", G_CALLBACK(Impl::onBufferRendered), m.get());
    g_signal_connect(m->webView, "load-changed", G_CALLBACK(Impl::onLoadChanged), m.get());
    g_signal_connect(m->webView, "notify::title", G_CALLBACK(Impl::onTitleChanged), m.get());
}

void HeadlessView::injectAtDocumentStart(const QString& source)
{
    const QByteArray utf8 = source.toUtf8();
    WebKitUserScript* script = webkit_user_script_new(
        utf8.constData(),
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        nullptr, nullptr);
    webkit_user_content_manager_add_script(m->ucm, script);
    webkit_user_script_unref(script);
}

void HeadlessView::load(const QUrl& url)
{
    m->loaded = false;
    m->latest = QImage();
    webkit_web_view_load_uri(m->webView, url.toString().toUtf8().constData());
}

void HeadlessView::loadHtml(const QString& html, const QUrl& baseUrl)
{
    m->loaded = false;
    m->latest = QImage();
    const QByteArray utf8 = html.toUtf8();
    const QByteArray base = baseUrl.isEmpty() ? QByteArray("about:blank")
                                              : baseUrl.toString().toUtf8();
    webkit_web_view_load_html(m->webView, utf8.constData(), base.constData());
}

bool HeadlessView::waitUntil(const std::function<bool()>& done, int timeoutMs)
{
    const gint64 deadline = g_get_monotonic_time() + static_cast<gint64>(timeoutMs) * 1000;
    while (!done()) {
        if (g_get_monotonic_time() > deadline)
            return false;
        g_main_context_iteration(nullptr, TRUE);
    }
    return true;
}

bool HeadlessView::waitUntilLoaded(int timeoutMs)
{
    return waitUntil([this] { return m->loaded; }, timeoutMs);
}

QString HeadlessView::title() const
{
    const char* t = webkit_web_view_get_title(m->webView);
    return t ? QString::fromUtf8(t) : QString();
}

QImage HeadlessView::frame() const
{
    return m->latest;
}

bool HeadlessView::hasFrame() const
{
    return !m->latest.isNull();
}

void HeadlessView::clearFrame()
{
    m->latest = QImage();
}

void HeadlessView::click(double x, double y)
{
    const guint32 now = static_cast<guint32>(g_get_monotonic_time() / 1000);
    WPEEvent* down = wpe_event_pointer_button_new(
        WPE_EVENT_POINTER_DOWN, m->view, WPE_INPUT_SOURCE_MOUSE,
        now, static_cast<WPEModifiers>(0), 1, x, y, 1);
    WPEEvent* up = wpe_event_pointer_button_new(
        WPE_EVENT_POINTER_UP, m->view, WPE_INPUT_SOURCE_MOUSE,
        now, static_cast<WPEModifiers>(0), 1, x, y, 0);
    wpe_view_event(m->view, down);
    wpe_view_event(m->view, up);
    wpe_event_unref(down);
    wpe_event_unref(up);
}

void HeadlessView::scroll(double x, double y, double deltaX, double deltaY)
{
    const guint32 now = static_cast<guint32>(g_get_monotonic_time() / 1000);
    WPEEvent* event = wpe_event_scroll_new(
        m->view, WPE_INPUT_SOURCE_TOUCHPAD, now, static_cast<WPEModifiers>(0),
        deltaX, deltaY, TRUE, FALSE, x, y);
    wpe_view_event(m->view, event);
    wpe_event_unref(event);
}

void HeadlessView::setLoadFinishedCallback(std::function<void(bool)> cb)
{
    m->onLoadFinished = std::move(cb);
}

void HeadlessView::setFrameCallback(std::function<void()> cb)
{
    m->onFrame = std::move(cb);
}

void HeadlessView::setTitleCallback(std::function<void(const QString&)> cb)
{
    m->onTitle = std::move(cb);
}

void HeadlessView::resize(int width, int height)
{
    if (width < 1 || height < 1)
        return;
    m->width = width;
    m->height = height;
    m->latest = QImage();
    wpe_view_resized(m->view, width, height);
}

QVariant HeadlessView::evaluateJavaScript(const QString& script)
{
    const QByteArray utf8 = script.toUtf8();
    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    struct State {
        GMainLoop* loop = nullptr;
        QVariant result;
    } state{loop, {}};

    webkit_web_view_evaluate_javascript(
        m->webView, utf8.constData(), utf8.size(), nullptr, nullptr, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer user_data) {
            auto* state = static_cast<State*>(user_data);
            GError* error = nullptr;
            JSCValue* value = webkit_web_view_evaluate_javascript_finish(
                WEBKIT_WEB_VIEW(src), res, &error);
            if (error) {
                g_error_free(error);
            } else if (value) {
                if (jsc_value_is_number(value))
                    state->result = jsc_value_to_double(value);
                else if (jsc_value_is_boolean(value))
                    state->result = jsc_value_to_boolean(value);
                else if (jsc_value_is_string(value)) {
                    char* s = jsc_value_to_string(value);
                    state->result = QString::fromUtf8(s);
                    g_free(s);
                } else if (jsc_value_is_null(value) || jsc_value_is_undefined(value)) {
                    state->result = QVariant();
                }
            }
            g_main_loop_quit(state->loop);
        },
        &state);

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    return state.result;
}

} // namespace wpe_webcontent
