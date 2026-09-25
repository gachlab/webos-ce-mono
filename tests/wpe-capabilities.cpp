// Can WPE WebKit (GObject + WPEPlatform headless) do what WebAppMgr needs?
// Same five capabilities as webengine-capabilities.cpp, without QtWebEngine:
//
//  1. render     a page drawn headless and read back into pixels
//  2. input      a click sent to the WPEView reaches the page's JavaScript
//  3. injection  an object the page can see before its own scripts run
//  4. resource   a synchronous read of a local file from JavaScript
//  5. native     a synchronous call from JavaScript answered by C++ here,
//                through a URL scheme of our own (webos-bridge:///)
//
// Spike for #81: prove the contract before wiring WebAppMgr.

#include <wpe/webkit.h>
#include <wpe/headless/wpe-headless.h>

#include <glib.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

struct BridgeState {
    int calls = 0;
};

struct FrameState {
    bool have = false;
    int width = 0;
    int height = 0;
    guint8 r = 0;
    guint8 g = 0;
    guint8 b = 0;
};

static void on_bridge(WebKitURISchemeRequest* request, gpointer user_data)
{
    auto* bridge = static_cast<BridgeState*>(user_data);
    ++bridge->calls;

    const char* path = webkit_uri_scheme_request_get_path(request);
    // Path form: /echo/ping  (three-slash URI with empty host)
    const char* echo = "/echo/";
    std::string body = "native:";
    if (path && std::strncmp(path, echo, std::strlen(echo)) == 0)
        body += (path + std::strlen(echo));

    GInputStream* stream = g_memory_input_stream_new_from_data(
        g_strdup(body.c_str()), static_cast<gssize>(body.size()), g_free);
    webkit_uri_scheme_request_finish(request, stream,
                                     static_cast<gint64>(body.size()), "text/plain");
    g_object_unref(stream);
}

static void on_buffer_rendered(WPEView*, WPEBuffer* buffer, gpointer user_data)
{
    auto* frame = static_cast<FrameState*>(user_data);
    GError* error = nullptr;
    GBytes* bytes = wpe_buffer_import_to_pixels(buffer, &error);
    if (!bytes) {
        if (error) {
            g_printerr("import_to_pixels: %s\n", error->message);
            g_error_free(error);
        }
        return;
    }

    const int width = wpe_buffer_get_width(buffer);
    const int height = wpe_buffer_get_height(buffer);
    gsize len = 0;
    const auto* px = static_cast<const guint8*>(g_bytes_get_data(bytes, &len));
    if (!px || width < 1 || height < 1 || len < static_cast<gsize>(width * height * 4))
        return;

    // Sample the centre. WPE_PIXEL_FORMAT_ARGB8888 is little-endian BGRA in memory.
    const int x = width / 2;
    const int y = height / 2;
    const gsize i = (static_cast<gsize>(y) * static_cast<gsize>(width) + static_cast<gsize>(x)) * 4;
    frame->width = width;
    frame->height = height;
    frame->b = px[i + 0];
    frame->g = px[i + 1];
    frame->r = px[i + 2];
    frame->have = true;
}

static void on_load_changed(WebKitWebView*, WebKitLoadEvent load_event, gpointer user_data)
{
    if (load_event == WEBKIT_LOAD_FINISHED)
        *static_cast<bool*>(user_data) = true;
}

static int report(const char* what, bool ok, const std::string& detail)
{
    std::printf("%-10s : %-4s %s\n", what, ok ? "yes" : "NO", detail.c_str());
    return ok ? 0 : 1;
}

static bool wait_until(const std::function<bool()>& done, int timeout_ms)
{
    const gint64 deadline = g_get_monotonic_time() + static_cast<gint64>(timeout_ms) * 1000;
    while (!done()) {
        if (g_get_monotonic_time() > deadline)
            return false;
        g_main_context_iteration(nullptr, TRUE);
    }
    return true;
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    BridgeState bridge;
    FrameState frame;
    bool loaded = false;
    int failures = 0;

    GError* error = nullptr;
    WPEDisplay* display = wpe_display_headless_new();
    if (!wpe_display_connect(display, &error)) {
        std::printf("FAIL: wpe_display_connect: %s\n", error ? error->message : "?");
        return 1;
    }

    WebKitWebContext* context = webkit_web_context_new();
    WebKitSecurityManager* security = webkit_web_context_get_security_manager(context);
    webkit_security_manager_register_uri_scheme_as_local(security, "webos-bridge");
    webkit_security_manager_register_uri_scheme_as_secure(security, "webos-bridge");
    webkit_security_manager_register_uri_scheme_as_cors_enabled(security, "webos-bridge");
    webkit_web_context_register_uri_scheme(context, "webos-bridge", on_bridge, &bridge, nullptr);

    WebKitUserContentManager* ucm = webkit_user_content_manager_new();
    WebKitUserScript* palm = webkit_user_script_new(
        "window.PalmSystem = { launchParams: '{\"from\":\"native\"}' };",
        WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, palm);
    webkit_user_script_unref(palm);

    WebKitSettings* settings = webkit_settings_new();
    webkit_settings_set_allow_file_access_from_file_urls(settings, TRUE);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, TRUE);

    gchar* tmp = g_dir_make_tmp("wpe-capabilities-XXXXXX", &error);
    if (!tmp) {
        std::printf("FAIL: mkdtemp: %s\n", error ? error->message : "?");
        return 1;
    }
    const std::string dir = tmp;
    g_free(tmp);

    {
        gchar* path = g_build_filename(dir.c_str(), "resource.txt", nullptr);
        g_file_set_contents(path, "from disk", -1, nullptr);
        g_free(path);
    }
    {
        const char* html = R"HTML(<!doctype html>
<html><head><script>
  var injected = (typeof PalmSystem !== "undefined") ? PalmSystem.launchParams : "missing";
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "resource.txt", false);
  var resource = "failed";
  try { xhr.send(); resource = xhr.responseText; } catch (e) { resource = "threw " + e; }
  var bridge = new XMLHttpRequest();
  var native = "failed";
  try {
    bridge.open("GET", "webos-bridge:///echo/ping", false);
    bridge.send();
    native = bridge.responseText;
  } catch (e) { native = "threw " + e; }
  document.title = "loaded|" + injected + "|" + resource + "|" + native;
  document.addEventListener("click", function (e) {
    document.title = "clicked|" + e.clientX + "," + e.clientY;
  });
</script></head>
<body style="margin:0; background:#ff00c8; width:200px; height:200px"></body></html>
)HTML";
        gchar* path = g_build_filename(dir.c_str(), "page.html", nullptr);
        g_file_set_contents(path, html, -1, nullptr);
        g_free(path);
    }

    WebKitWebView* web_view = WEBKIT_WEB_VIEW(g_object_new(
        WEBKIT_TYPE_WEB_VIEW,
        "display", display,
        "web-context", context,
        "user-content-manager", ucm,
        "settings", settings,
        nullptr));

    WPEView* view = webkit_web_view_get_wpe_view(web_view);
    wpe_view_resized(view, 200, 200);
    wpe_view_map(view);
    wpe_view_focus_in(view);

    g_signal_connect(view, "buffer-rendered", G_CALLBACK(on_buffer_rendered), &frame);
    g_signal_connect(web_view, "load-changed", G_CALLBACK(on_load_changed), &loaded);

    gchar* page_uri = g_filename_to_uri(
        (dir + "/page.html").c_str(), nullptr, nullptr);
    webkit_web_view_load_uri(web_view, page_uri);
    g_free(page_uri);

    if (!wait_until([&] { return loaded; }, 30000)) {
        std::printf("FAIL: the page did not load within 30 s\n");
        return 1;
    }

    std::string title;
    wait_until([&] {
        const char* t = webkit_web_view_get_title(web_view);
        if (!t || std::strncmp(t, "loaded|", 7) != 0)
            return false;
        title = t;
        return true;
    }, 5000);

    auto part = [&](int index) -> std::string {
        size_t start = 0;
        for (int i = 0; i < index; ++i) {
            const size_t bar = title.find('|', start);
            if (bar == std::string::npos)
                return {};
            start = bar + 1;
        }
        const size_t bar = title.find('|', start);
        return bar == std::string::npos ? title.substr(start) : title.substr(start, bar - start);
    };

    failures += report("injection", part(1) == "{\"from\":\"native\"}",
                       "PalmSystem.launchParams seen by the page: " + part(1));
    failures += report("resource", part(2) == "from disk",
                       "synchronous XHR returned: " + part(2));
    failures += report("native", part(3) == "native:ping",
                       "synchronous XHR to webos-bridge:// returned: " + part(3)
                           + " (handler calls: " + std::to_string(bridge.calls) + ")");

    const bool rendered = wait_until([&] {
        return frame.have && frame.r == 0xff && frame.g == 0x00 && frame.b == 0xc8;
    }, 10000);
    char colour[16];
    std::snprintf(colour, sizeof(colour), "#%02x%02x%02x", frame.r, frame.g, frame.b);
    failures += report("render", rendered,
                       std::string("pixel read back: ") + colour
                           + " (" + std::to_string(frame.width) + "x"
                           + std::to_string(frame.height) + ")");

    WPEEvent* down = wpe_event_pointer_button_new(
        WPE_EVENT_POINTER_DOWN, view, WPE_INPUT_SOURCE_MOUSE,
        static_cast<guint32>(g_get_monotonic_time() / 1000), static_cast<WPEModifiers>(0),
        1, 30.0, 40.0, 1);
    WPEEvent* up = wpe_event_pointer_button_new(
        WPE_EVENT_POINTER_UP, view, WPE_INPUT_SOURCE_MOUSE,
        static_cast<guint32>(g_get_monotonic_time() / 1000), static_cast<WPEModifiers>(0),
        1, 30.0, 40.0, 0);
    wpe_view_event(view, down);
    wpe_view_event(view, up);
    wpe_event_unref(down);
    wpe_event_unref(up);

    const bool clicked = wait_until([&] {
        const char* t = webkit_web_view_get_title(web_view);
        if (!t || std::strncmp(t, "clicked|", 8) != 0)
            return false;
        title = t;
        return true;
    }, 5000);
    failures += report("input", clicked && title == "clicked|30,40",
                       "title after the click: " + title);

    g_object_unref(web_view);
    g_object_unref(settings);
    g_object_unref(ucm);
    g_object_unref(context);
    g_object_unref(display);

    std::printf("%s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
