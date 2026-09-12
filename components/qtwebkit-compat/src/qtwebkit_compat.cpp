#include "qtwebkit_compat.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QQuickWidget>
#include <QSet>
#include <QStyleOptionGraphicsItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrlQuery>
#include <QWebEngineFrame>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>

namespace {

const char kScheme[] = "webos-bridge";
const char kInjectedScriptName[] = "webos-document-creation";
const char kBorderImageScriptName[] = "webos-border-image";
const char kPrefixedEventScriptName[] = "webos-prefixed-events";
const char kAppViewShimScriptName[] = "webos-app-view-shims";
const char kFrameCancelScriptName[] = "webos-frame-cancel";

// Before QApplication exists: a URL scheme can only be registered then, and
// QtWebEngine wants shared GL contexts decided before the first one is made.
void beforeApplication()
{
    QWebEngineUrlScheme scheme(kScheme);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    // LocalScheme and SecureScheme are what let a file:// page call it.
    scheme.setFlags(QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled
                    | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // WebAppMgr only ever draws pages offscreen and reads them back; Chromium's
    // GPU process loses its context there ("Context lost during MakeCurrent").
    // An explicit choice in the environment still wins.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_CHROMIUM_FLAGS"))
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");
}
Q_CONSTRUCTOR_FUNCTION(beforeApplication)

// ---------------------------------------------------------------------------
// The WebView methods the mail app calls on a view that is not one.
//
// MessageDisplay renders a message body into "DivHtmlView" -- plain DOM, the
// WebView-backed body next to it is commented out in HP's own source ("this
// sauce is weak. So weak."). But its rendered() still calls setRedirects() on
// that body, guarded only by "is PalmSystem here". On a device both were true
// at once; here PalmSystem exists and the browser plugin does not, so the call
// threw and took _unhideMainApp() with it on its first line. The mail card then
// never selected its mail view and painted it under the first-launch screen.
//
// setRedirects and cancelDialog belong to the browser plugin -- rules for URLs
// it should hand back instead of navigating, and the dialogs it raises. A div
// neither navigates nor raises them. setHTML is a body handed over rather than
// read from disk, so it goes through the same sanitise-and-show path loadPage()
// uses. Nothing here patches over a mistake: these are the plugin's side of an
// interface the app still speaks.
const char kAppViewShims[] = R"JS(
(function () {
    if (window.__webosAppViewShims)
        return;
    window.__webosAppViewShims = true;

    function patch(ctor) {
        var proto = ctor && ctor.prototype;
        if (!proto || proto.__webosViewShimmed)
            return false;
        proto.__webosViewShimmed = true;

        if (!proto.setRedirects)
            proto.setRedirects = function () {};

        if (!proto.cancelDialog)
            proto.cancelDialog = function () {};

        if (!proto.setHTML) {
            proto.setHTML = function (url, html) {
                if (url)
                    this.url = url;
                this.contentType = "text/html";
                var clean = this.loadedAndSanitize ? this.loadedAndSanitize(html || "", this.contentType)
                                                   : (html || "");
                if (this.$ && this.$.wrapper)
                    this.$.wrapper.setContent(clean);
                if (this.fitWidth)
                    this.fitWidth();
                this.viewReady = true;
                if (this.doViewReady)
                    this.doViewReady();
            };
        }
        return true;
    }

    // enyo.kind publishes a kind under its name (Oop.js: enyo.setObject), and
    // the app defines this one after this script runs -- but in the same burst
    // of script evaluation, well before any timer of ours could fire. Polling
    // lost that race: the body was rendered, and setRedirects called, between
    // two ticks. So catch the assignment itself.
    var held;
    try {
        Object.defineProperty(window, "DivHtmlView", {
            configurable: true,
            enumerable: true,
            get: function () { return held; },
            set: function (ctor) {
                held = ctor;
                patch(ctor);
            }
        });
    } catch (e) {
        // If the property cannot be redefined, fall back to looking for it.
        var tries = 0;
        var timer = setInterval(function () {
            if (typeof DivHtmlView !== "undefined" && patch(DivHtmlView))
                clearInterval(timer);
            else if (++tries > 600)
                clearInterval(timer);
        }, 50);
    }
})();
)JS";

// ---------------------------------------------------------------------------
// The prefixed transition and animation events enyo still listens for.
//
// Chromium no longer fires webkitTransitionEnd; it fires transitionend and
// nothing else. Measured here: a listener on the prefixed name is called 0
// times, on the plain name once. enyo registers only the prefixed name -- 19
// places, 6 of them addEventListener -- and its Pane keeps a transition "in
// flight" until that handler runs. Pane.flow() only applies display:none to a
// view that is not the current one AND not transitioning, so the outgoing view
// was never hidden: the mail card painted its first-launch screen and its
// three-pane view on top of each other, which read on screen as transparency.
//
// The listener is registered for the modern name as well, so code written for
// 2010 WebKit is called when the event actually happens.
const char kPrefixedEvents[] = R"JS(
(function () {
    if (window.__webosPrefixedEvents)
        return;
    window.__webosPrefixedEvents = true;

    var modernName = {
        webkittransitionend: "transitionend",
        webkitanimationend: "animationend",
        webkitanimationstart: "animationstart",
        webkitanimationiteration: "animationiteration"
    };

    var add = EventTarget.prototype.addEventListener;
    var remove = EventTarget.prototype.removeEventListener;

    EventTarget.prototype.addEventListener = function (type, listener, options) {
        var modern = modernName[String(type).toLowerCase()];
        if (modern)
            add.call(this, modern, listener, options);
        return add.call(this, type, listener, options);
    };

    EventTarget.prototype.removeEventListener = function (type, listener, options) {
        var modern = modernName[String(type).toLowerCase()];
        if (modern)
            remove.call(this, modern, listener, options);
        return remove.call(this, type, listener, options);
    };
})();
)JS";

// ---------------------------------------------------------------------------
// The prefixed frame canceller, so enyo stops cancelling other people's timers.
//
// enyo sets the pair up in dom/util.js:
//
//     var builtin = window.webkitRequestAnimationFrame;
//     enyo.requestAnimationFrame = builtin ? enyo.bind(window, builtin) : ...
//     var builtin = window.webkitCancelRequestAnimationFrame || window.clearTimeout;
//     enyo.cancelRequestAnimationFrame = enyo.bind(window, builtin);
//
// Chromium still has webkitRequestAnimationFrame but dropped
// webkitCancelRequestAnimationFrame, so that || settles on clearTimeout: enyo
// takes handles from the frame scheduler and hands them to the timer one. The
// two number their handles independently, so a cancel clears whichever timeout
// holds that number. Measured in the shell:
// enyo.cancelRequestAnimationFrame(55) killed a plain setTimeout whose id was
// 55, and the scroller cancels a frame 12245 times in 14 seconds.
//
// That is what stopped the mail card's fade. enyo.transitions.Fade drives the
// animation from a setTimeout chain kept in one handle; the scroller cancels a
// frame numbered the same, the chain never ticks again, and because only the
// Fade's done() clears Pane._transitioning, the pane stays "transitioning"
// forever -- the outgoing view is left half faded over the incoming one and
// every later view change is queued and never served.
//
// Giving the prefixed name back lets HP's || find it, with no change to enyo.
const char kFrameCancel[] = R"JS(
(function () {
    if (typeof window.webkitCancelRequestAnimationFrame === "function")
        return;
    if (typeof window.cancelAnimationFrame !== "function")
        return;
    window.webkitCancelRequestAnimationFrame = function (handle) {
        return window.cancelAnimationFrame(handle);
    };
})();
)JS";

// ---------------------------------------------------------------------------
// The border box -webkit-border-image used to imply.
//
// In the WebKit webOS shipped, an element with a border image took its
// border-width even though no border-style was ever declared. Chromium computes
// that border to 0, and 75 of the 94 stylesheets in this tree put border-width
// next to -webkit-border-image and no border-style at all -- so those controls
// lose both their artwork and the space the artwork used to occupy.
//
// The calculator shows what that costs. Its keys carry a 15px border image;
// without it a key measures 121x94 instead of 91x64, and the app sizes its own
// font from the key it measures (Calculator.js: floor(min(h, w) * 0.9)), so it
// picks 84px where it used to pick 57px and every two-character label -- MC, M+,
// M-, MR -- spills out of its key. Giving the border box back restores the
// layout HP designed, without touching HP's stylesheets.
const char kBorderImageCompat[] = R"JS(
(function () {
    if (window.__webosBorderImage)
        return;
    window.__webosBorderImage = true;

    function patchSheet(sheet) {
        var rules, changed = 0;
        // A stylesheet from another origin does not hand over its rules.
        try { rules = sheet.cssRules; } catch (e) { return 0; }
        if (!rules)
            return 0;
        for (var r = 0; r < rules.length; r++) {
            var style = rules[r].style;
            if (!style)
                continue;
            var image = style.getPropertyValue("-webkit-border-image")
                     || style.getPropertyValue("border-image")
                     || style.getPropertyValue("border-image-source");
            if (!image || image === "none")
                continue;
            if (style.getPropertyValue("border-style"))
                continue;
            style.setProperty("border-style", "solid");
            if (!style.getPropertyValue("border-color"))
                style.setProperty("border-color", "transparent");
            changed++;
        }
        return changed;
    }

    function patch() {
        var changed = 0;
        for (var s = 0; s < document.styleSheets.length; s++)
            changed += patchSheet(document.styleSheets[s]);
        if (!changed)
            return;

        // Everything just got smaller by the border, and an app that measured
        // itself first is still holding the old number. The calculator reads a
        // key back and sizes its font from it, so it kept 84px for a key that is
        // now 71px wide and the labels came out worse than with no border at
        // all. A resize is what these apps -- and enyo's own controls -- listen
        // to in order to measure again.
        window.setTimeout(function () {
            window.dispatchEvent(new Event("resize"));
        }, 0);
    }

    document.addEventListener("DOMContentLoaded", patch);
    window.addEventListener("load", patch);

    // enyo adds its stylesheets from script (dom.js: makeElement("link")), so
    // some of them arrive after both of those events.
    if (window.MutationObserver) {
        new MutationObserver(function (records) {
            for (var i = 0; i < records.length; i++) {
                var added = records[i].addedNodes;
                for (var n = 0; n < added.length; n++) {
                    var node = added[n];
                    if (!node.tagName)
                        continue;
                    var tag = node.tagName.toLowerCase();
                    if (tag === "style")
                        patch();
                    else if (tag === "link")
                        node.addEventListener("load", patch);
                }
            }
        // document, not documentElement: this runs at document creation, and
        // there is no <html> yet to observe.
        }).observe(document, { childList: true, subtree: true });
    }
})();
)JS";

// ---------------------------------------------------------------------------
// The JavaScript side of the object bridge. Runs once per document, first.

const char kBridgeCore[] = R"JS(
(function () {
    if (window.__webosBridge)
        return;
    var proxies = {};

    function unwrap(value) {
        if (value && typeof value === "object") {
            if (value.__webosObject !== undefined)
                return proxy(value.__webosObject, value.meta);
            if (Array.isArray(value))
                return value.map(unwrap);
        }
        return value;
    }

    function request(id, op, name, args) {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "webos-bridge:///" + id + "/" + op + "/" + encodeURIComponent(name)
                 + "?a=" + encodeURIComponent(JSON.stringify(args || [])), false);
        xhr.send();
        var reply = JSON.parse(xhr.responseText);
        if (reply.e !== undefined)
            throw new Error(reply.e);
        return unwrap(reply.v);
    }

    function proxy(id, meta) {
        if (proxies[id])
            return proxies[id];
        var object = {};
        meta.properties.forEach(function (name) {
            Object.defineProperty(object, name, {
                get: function () { return request(id, "get", name); },
                set: function (value) { request(id, "set", name, [value]); },
                enumerable: true
            });
        });
        meta.methods.forEach(function (name) {
            object[name] = function () {
                return request(id, "call", name, Array.prototype.slice.call(arguments));
            };
        });
        meta.signals.forEach(function (name) {
            var handlers = [];
            object[name] = {
                connect: function (fn) { handlers.push(fn); },
                disconnect: function (fn) {
                    var i = handlers.indexOf(fn);
                    if (i >= 0)
                        handlers.splice(i, 1);
                },
                __handlers: handlers
            };
        });
        proxies[id] = object;
        return object;
    }

    window.__webosBridge = {
        proxy: proxy,
        emit: function (id, name, args) {
            var object = proxies[id];
            if (!object || !object[name] || !object[name].__handlers)
                return;
            object[name].__handlers.slice().forEach(function (fn) {
                fn.apply(null, args.map(unwrap));
            });
        }
    };
})();
)JS";

// ---------------------------------------------------------------------------
// The browser's content area, which used to be an NPAPI plugin.
//
// enyo's BasicWebView renders <object type="application/x-palm-browser"> and
// then talks to it as a plugin: adapterReady() asks whether this.node.openURL
// is there, _connect() calls this.node.connectBrowserServer(), and initView()
// calls interrogateClicks, setShowClickedLink and pageFocused on the node. Once
// connected, EVERY setting and command in that control funnels through one
// method, callBrowserAdapter -- even urlChanged, which sends "openURL".
//
// So the control does not need rewriting. It needs its plugin to exist. This
// gives the node the handful of methods it probes for, answers
// connectBrowserServer by reporting the connection straight back, and routes
// callBrowserAdapter to a real page that BrowserViewAdapter created and the
// host page paints inside itself (QWebPage::embedPage). HP's control then runs
// unchanged, believing it has its plugin.
//
// It matters that nothing here reaches for BrowserViewFactory until the control
// is actually rendered: this script runs at DocumentCreation, and the factory
// is published when the bridge collects its objects.

const char kBrowserViewScriptName[] = "webos-browser-view";

const char kBrowserView[] = R"JS(
(function () {
    if (window.__webosBrowserView)
        return;
    window.__webosBrowserView = true;

    // Where the hole is, in the page's own coordinates: HP's control only ever
    // reports a size, and the host has to know where to paint.
    function boundsOf(node) {
        var r = node.getBoundingClientRect();
        return {
            x: Math.round(r.left + (window.pageXOffset || 0)),
            y: Math.round(r.top + (window.pageYOffset || 0)),
            w: Math.round(r.width),
            h: Math.round(r.height)
        };
    }

    // Every hole on this page, so one popup can re-measure all of them.
    var holes = [];

    function setRect(control, b) {
        var last = control.__webosRect;
        if (last && last.x === b.x && last.y === b.y && last.w === b.w && last.h === b.h)
            return;
        control.__webosRect = b;
        control.__webosView.setGeometry(b.x, b.y, b.w, b.h);
    }

    // An empty rect is how the page says "not now". The host keeps the page and
    // its viewport and simply stops blitting, so the app's own pixels show.
    function suspend(control) {
        if (control.__webosView)
            setRect(control, {x: 0, y: 0, w: 0, h: 0});
    }

    // Anything the app draws over the hole.
    //
    // The blit goes on top of everything the page painted, so whatever the app
    // opens across the content area ends up underneath it. Measured on the
    // running browser: its action bar menu is an absolutely positioned
    // "enyo-popup enyo-popup-menu launch-popup" at [727, 30, 153, 164], z-index
    // 123, over a hole starting at y 54 -- so its lower 140 pixels were painted
    // over and it looked like it had opened behind the page.
    //
    // Full-page containers are not overlays: the hole's own ancestors are
    // absolute and as large as the view.
    function covered(node, b) {
        var all = document.querySelectorAll("*");
        for (var i = 0; i < all.length; i++) {
            var e = all[i];
            if (e === node || e.contains(node) || node.contains(e))
                continue;
            var style = window.getComputedStyle(e);
            if (style.position !== "absolute" && style.position !== "fixed")
                continue;
            if (style.display === "none" || style.visibility === "hidden" || style.opacity === "0")
                continue;
            var r = e.getBoundingClientRect();
            if (r.width < 8 || r.height < 8)
                continue;
            if (r.width >= b.w && r.height >= b.h)
                continue;
            if (r.right <= b.x || r.left >= b.x + b.w ||
                r.bottom <= b.y || r.top >= b.y + b.h)
                continue;
            return true;
        }
        return false;
    }

    function sendGeometry(control, attempt) {
        var node = control.hasNode && control.hasNode();
        if (!node || !control.__webosView)
            return;
        var b = boundsOf(node);
        if (b.w <= 0 || b.h <= 0) {
            // Nothing to paint into. Either the pane that owns this view has
            // not revealed it -- the browser opens on its start page, and enyo
            // keeps the others at display:none -- or it just has, and the
            // layout is not settled in the same turn. Stop blitting meanwhile,
            // which is also what keeps a backgrounded tab from painting over
            // the one in front, and measure again for a moment.
            suspend(control);
            attempt = attempt || 0;
            if (attempt < 12)
                setTimeout(function () { sendGeometry(control, attempt + 1); }, 50);
            return;
        }
        if (covered(node, b)) {
            suspend(control);
            return;
        }
        setRect(control, b);
    }

    function remeasureAll() {
        for (var i = 0; i < holes.length; i++)
            sendGeometry(holes[i]);
    }

    // The verbs isis-browser actually sends. Everything else the control emits
    // on its way up -- pageFocused, setEnableJavaScript, addUrlRedirect,
    // handleFlick and the rest -- is taken and dropped: either the engine
    // already does it, or nothing depends on it yet.
    function command(control, name, args) {
        var view = control.__webosView;
        if (!view)
            return;
        args = args || [];
        switch (name) {
        case "openURL":        view.setUrl(String(args[0] || "")); break;
        case "goBack":         view.goBack(); break;
        case "goForward":      view.goForward(); break;
        case "reloadPage":     view.reload(); break;
        case "stopLoad":       view.stop(); break;
        case "findInPage":     view.findInPage(String(args[0] || "")); break;
        case "setVisibleSize": sendGeometry(control); break;
        default: break;
        }
    }

    function attach(control) {
        var node = control.hasNode && control.hasNode();
        if (!node || node.__webosBrowserNode)
            return;
        node.__webosBrowserNode = true;

        // BasicWebView gives its node tabIndex 0 because the plugin had to be
        // able to take keyboard focus. There is no plugin now, and a focusable
        // box across the whole content area takes the focus away from the
        // address bar: measured with the browser open, document.activeElement
        // was this div. Whatever the embedded page needs will reach it through
        // input routing, not through this element.
        node.removeAttribute("tabindex");
        node.tabIndex = -1;

        if (!window.BrowserViewFactory)
            return;
        var view = window.BrowserViewFactory.create();
        if (!view)
            return;
        control.__webosView = view;
        holes.push(control);

        // What the control probes for before it will talk to a plugin.
        node.openURL = function () {};
        node.setPageIdentifier = function () {};
        node.interrogateClicks = function () {};
        node.setShowClickedLink = function () {};
        node.pageFocused = function () {};
        node.clearHistory = function () {};
        node.getHistoryState = function () {};
        node.connectBrowserServer = function () {
            // The server it is waiting for is the engine we already have. Reply
            // on a turn of its own, as an IPC answer would have arrived.
            setTimeout(function () {
                if (control.serverConnected)
                    control.serverConnected();
                sendGeometry(control);
            }, 0);
        };

        // The chrome only moves its progress bar, its title and its back and
        // forward buttons if the view tells it to.
        view.loadStarted.connect(function () {
            if (control.doLoadStarted) control.doLoadStarted();
        });
        view.loadProgress.connect(function (progress) {
            if (control.doLoadProgress) control.doLoadProgress(progress);
        });
        view.loadFinished.connect(function () {
            if (control.doLoadComplete) control.doLoadComplete();
        });
        view.titleChanged.connect(function (title) {
            if (control.doPageTitleChanged)
                control.doPageTitleChanged(title, view.url(),
                                           view.canGoBack(), view.canGoForward());
        });
    }

    function patch(BasicWebView) {
        var proto = BasicWebView && BasicWebView.prototype;
        if (!proto || proto.__webosBrowserViewPatched)
            return false;
        proto.__webosBrowserViewPatched = true;

        // No plugin to instantiate, so no <object>: a plain box, and the host
        // page paints the real view over it.
        proto.nodeTag = "div";

        // Every popup in this framework goes through one method to show and to
        // hide, so that is where a hole learns it has been covered or freed.
        var enyo = window.enyo;
        if (enyo && enyo.Popup && enyo.Popup.prototype &&
                !enyo.Popup.prototype.__webosHoleAware) {
            enyo.Popup.prototype.__webosHoleAware = true;
            var showingBefore = enyo.Popup.prototype.showingChanged;
            enyo.Popup.prototype.showingChanged = function () {
                var r = showingBefore ? showingBefore.apply(this, arguments) : undefined;
                // After the popup's own display has been applied, not before.
                setTimeout(remeasureAll, 0);
                return r;
            };
        }

        var renderedBefore = proto.rendered;
        proto.rendered = function () {
            attach(this);
            return renderedBefore ? renderedBefore.apply(this, arguments) : undefined;
        };

        proto.callBrowserAdapter = function (name, args) {
            command(this, name, args);
        };

        var resizeBefore = proto.resize;
        proto.resize = function () {
            var r = resizeBefore ? resizeBefore.apply(this, arguments) : undefined;
            sendGeometry(this);
            return r;
        };
        return true;
    }

    // enyo.BasicWebView is defined when the framework loads, which is after
    // this script runs. Catch the assignment rather than poll for it: polling
    // lost that race once already, for the mail app's view.
    function watchEnyo(enyo) {
        if (!enyo)
            return;
        if (enyo.BasicWebView) {
            patch(enyo.BasicWebView);
            return;
        }
        var held;
        try {
            Object.defineProperty(enyo, "BasicWebView", {
                configurable: true,
                enumerable: true,
                get: function () { return held; },
                set: function (ctor) { held = ctor; patch(ctor); }
            });
        } catch (e) {
            var tries = 0;
            var timer = setInterval(function () {
                if (enyo.BasicWebView && patch(enyo.BasicWebView))
                    clearInterval(timer);
                else if (++tries > 600)
                    clearInterval(timer);
            }, 50);
        }
    }

    if (window.enyo) {
        watchEnyo(window.enyo);
        return;
    }
    var heldEnyo;
    try {
        Object.defineProperty(window, "enyo", {
            configurable: true,
            enumerable: true,
            get: function () { return heldEnyo; },
            set: function (value) { heldEnyo = value; watchEnyo(value); }
        });
    } catch (e) {
        var enyoTries = 0;
        var enyoTimer = setInterval(function () {
            if (window.enyo) {
                watchEnyo(window.enyo);
                clearInterval(enyoTimer);
            } else if (++enyoTries > 600) {
                clearInterval(enyoTimer);
            }
        }, 50);
    }
})();
)JS";

// ---------------------------------------------------------------------------
// The C++ side: which QObject each id is, and what JavaScript may reach.

struct Published
{
    QPointer<QObject> object;
    QPointer<QWebEnginePage> page;   // where its signals are delivered
};

QHash<int, Published>& published()
{
    static QHash<int, Published> table;
    return table;
}

int publishObject(QObject* object, QWebEnginePage* page);

bool isOwnMember(const QMetaObject* meta, int index, bool method)
{
    // QObject's own members (destroyed, deleteLater, objectName...) stay out.
    const int offset = method ? QObject::staticMetaObject.methodCount()
                              : QObject::staticMetaObject.propertyCount();
    Q_UNUSED(meta);
    return index >= offset;
}

QJsonObject describe(const QMetaObject* meta)
{
    QJsonArray properties, methods, signalNames;
    for (int i = 0; i < meta->propertyCount(); ++i)
        if (isOwnMember(meta, i, false))
            properties.append(QString::fromLatin1(meta->property(i).name()));
    QSet<QString> seen;
    for (int i = 0; i < meta->methodCount(); ++i) {
        if (!isOwnMember(meta, i, true))
            continue;
        const QMetaMethod m = meta->method(i);
        if (m.access() != QMetaMethod::Public)
            continue;
        const QString name = QString::fromLatin1(m.name());
        if (seen.contains(name))
            continue;
        seen.insert(name);
        if (m.methodType() == QMetaMethod::Signal)
            signalNames.append(name);
        else
            methods.append(name);
    }
    return QJsonObject{{"properties", properties}, {"methods", methods}, {"signals", signalNames}};
}

QJsonValue toJson(const QVariant& value, QWebEnginePage* page)
{
    if (value.metaType().flags() & QMetaType::PointerToQObject) {
        QObject* object = value.value<QObject*>();
        if (!object)
            return QJsonValue::Null;
        return QJsonObject{{"__webosObject", publishObject(object, page)},
                           {"meta", describe(object->metaObject())}};
    }
    if (!value.isValid())
        return QJsonValue::Null;
    return QJsonValue::fromVariant(value);
}

// Runs one script in every frame of a page, depth first.
//
// QWebEnginePage::runJavaScript reaches the main frame and no other, while the
// bridge core keeps its proxies, and the handlers connected to them, in a map
// private to each frame. A reply to an object that a child frame proxied was
// landing in the main frame's map, which had never heard of that id, so
// __webosBridge.emit returned without calling anything. Ids come from a single
// counter for the whole page, so running this everywhere reaches exactly the
// frame that owns the object: in every other one emit finds nothing and stops.
void runInEveryFrame(QWebEngineFrame frame, const QString& script)
{
    if (!frame.isValid())
        return;
    frame.runJavaScript(script);
    for (QWebEngineFrame child : frame.children())
        runInEveryFrame(child, script);
}

// Receives one signal of one object and hands its arguments to JavaScript. It
// is QSignalSpy's technique: connect to a method index just past QObject's own
// and catch the call in qt_metacall.
class SignalRelay : public QObject
{
public:
    SignalRelay(QObject* sender, const QMetaMethod& signal, int id, QWebEnginePage* page)
        : QObject(sender), m_signal(signal), m_id(id), m_page(page)
    {
        QMetaObject::connect(sender, signal.methodIndex(), this,
                             QObject::staticMetaObject.methodCount(), Qt::DirectConnection);
    }

    int qt_metacall(QMetaObject::Call call, int methodId, void** argv) override
    {
        methodId = QObject::qt_metacall(call, methodId, argv);
        if (methodId < 0)
            return methodId;
        if (call == QMetaObject::InvokeMetaMethod) {
            if (methodId == 0)
                deliver(argv);
            --methodId;
        }
        return methodId;
    }

private:
    void deliver(void** argv)
    {
        if (!m_page)
            return;
        QJsonArray args;
        for (int i = 0; i < m_signal.parameterCount(); ++i)
            args.append(toJson(QVariant(QMetaType(m_signal.parameterType(i)), argv[i + 1]), m_page));
        const QString script = QString("window.__webosBridge && window.__webosBridge.emit(%1, %2, %3);")
            .arg(m_id)
            .arg(QString::fromUtf8(QJsonDocument(QJsonArray{QString::fromLatin1(m_signal.name())}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
            .arg(QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)));
        runInEveryFrame(m_page->mainFrame(), script);
    }

    QMetaMethod m_signal;
    int m_id;
    QPointer<QWebEnginePage> m_page;
};

int publishObject(QObject* object, QWebEnginePage* page)
{
    static int nextId = 1;
    for (auto it = published().constBegin(); it != published().constEnd(); ++it)
        if (it.value().object == object)
            return it.key();

    const int id = nextId++;
    published().insert(id, Published{object, page});
    QObject::connect(object, &QObject::destroyed, [id]() { published().remove(id); });

    const QMetaObject* meta = object->metaObject();
    for (int i = QObject::staticMetaObject.methodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() == QMetaMethod::Signal && m.access() == QMetaMethod::Public)
            new SignalRelay(object, m, id, page);
    }
    return id;
}

QByteArray replyValue(const QJsonValue& value)
{
    return QJsonDocument(QJsonObject{{"v", value}}).toJson(QJsonDocument::Compact);
}

QByteArray replyError(const QString& message)
{
    return QJsonDocument(QJsonObject{{"e", message}}).toJson(QJsonDocument::Compact);
}

QByteArray invoke(QObject* object, QWebEnginePage* page, const QString& name, const QJsonArray& args)
{
    const QMetaObject* meta = object->metaObject();
    for (int i = QObject::staticMetaObject.methodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() == QMetaMethod::Signal || m.access() != QMetaMethod::Public)
            continue;
        if (QString::fromLatin1(m.name()) != name || m.parameterCount() != args.size())
            continue;
        if (args.size() > 10)
            return replyError(name + ": too many arguments");

        // Arguments, converted to what the method declares.
        QList<QVariant> values;
        values.reserve(args.size());
        for (int a = 0; a < args.size(); ++a) {
            QVariant v = args.at(a).toVariant();
            const QMetaType type(m.parameterType(a));
            if (type.id() != QMetaType::QVariant && !v.convert(type))
                v = QVariant(type);
            values.append(v);
        }
        QGenericArgument generic[10];
        for (int a = 0; a < values.size(); ++a) {
            const QMetaType type(m.parameterType(a));
            generic[a] = type.id() == QMetaType::QVariant
                ? QGenericArgument("QVariant", &values[a])
                : QGenericArgument(type.name(), values[a].constData());
        }

        QVariant result;
        bool ok;
        if (m.returnType() == QMetaType::Void) {
            ok = m.invoke(object, Qt::DirectConnection, generic[0], generic[1], generic[2], generic[3],
                          generic[4], generic[5], generic[6], generic[7], generic[8], generic[9]);
        } else {
            const QMetaType returnType(m.returnType());
            result = QVariant(returnType);
            ok = m.invoke(object, Qt::DirectConnection,
                          returnType.id() == QMetaType::QVariant
                              ? QGenericReturnArgument("QVariant", &result)
                              : QGenericReturnArgument(returnType.name(), result.data()),
                          generic[0], generic[1], generic[2], generic[3], generic[4],
                          generic[5], generic[6], generic[7], generic[8], generic[9]);
        }
        if (!ok)
            return replyError(name + ": the call failed");
        return replyValue(toJson(result, page));
    }
    return replyError(QString("%1 has no method %2 taking %3 arguments")
                          .arg(QString::fromLatin1(meta->className()), name).arg(args.size()));
}

class BridgeHandler : public QWebEngineUrlSchemeHandler
{
public:
    void requestStarted(QWebEngineUrlRequestJob* job) override
    {
        // webos-bridge:///<id>/<get|set|call>/<name>?a=<JSON array>
        const QStringList parts = job->requestUrl().path().split('/', Qt::SkipEmptyParts);
        const QJsonArray args = QJsonDocument::fromJson(
            QUrlQuery(job->requestUrl()).queryItemValue("a", QUrl::FullyDecoded).toUtf8()).array();

        QByteArray body;
        const Published entry = parts.size() == 3 ? published().value(parts[0].toInt()) : Published{};
        if (!entry.object) {
            body = replyError("no such object");
        } else {
            QObject* object = entry.object;
            const QString op = parts[1];
            const QString name = QUrl::fromPercentEncoding(parts[2].toUtf8());
            const int propertyIndex = object->metaObject()->indexOfProperty(name.toLatin1());
            if (op == "get" && propertyIndex >= 0) {
                body = replyValue(toJson(object->metaObject()->property(propertyIndex).read(object), entry.page));
            } else if (op == "set" && propertyIndex >= 0 && !args.isEmpty()) {
                object->metaObject()->property(propertyIndex).write(object, args.at(0).toVariant());
                body = replyValue(QJsonValue::Null);
            } else if (op == "call") {
                body = invoke(object, entry.page, name, args);
            } else {
                body = replyError(op + " " + name + ": not available");
            }
        }

        auto* device = new QBuffer(job);
        device->setData(body);
        job->reply("application/json", device);
    }
};

// ---------------------------------------------------------------------------
// The profile every page shares.

QWebEngineProfile* sharedProfile()
{
    static QWebEngineProfile* profile = nullptr;
    if (profile)
        return profile;

    // A named profile keeps its storage on disk; QtWebEngine's default one does
    // not. Its location follows PERSISTENT_STORAGE_PATH, as SysMgrWebPage's
    // paths do.
    profile = new QWebEngineProfile(QStringLiteral("webOS"), qApp);
    const QByteArray root = qgetenv("PERSISTENT_STORAGE_PATH");
    if (!root.isEmpty() && QDir().mkpath(QString::fromLocal8Bit(root) + "/webengine"))
        profile->setPersistentStoragePath(QString::fromLocal8Bit(root) + "/webengine");

    static BridgeHandler handler;
    profile->installUrlSchemeHandler(kScheme, &handler);
    return profile;
}

QWebEngineSettings::WebAttribute engineAttribute(QWebSettings::WebAttribute attribute, bool* exists)
{
    *exists = true;
    switch (attribute) {
    case QWebSettings::AutoLoadImages:                  return QWebEngineSettings::AutoLoadImages;
    case QWebSettings::JavascriptEnabled:               return QWebEngineSettings::JavascriptEnabled;
    case QWebSettings::PluginsEnabled:                  return QWebEngineSettings::PluginsEnabled;
    case QWebSettings::JavascriptCanOpenWindows:        return QWebEngineSettings::JavascriptCanOpenWindows;
    case QWebSettings::JavascriptCanAccessClipboard:    return QWebEngineSettings::JavascriptCanAccessClipboard;
    case QWebSettings::LinksIncludedInFocusChain:       return QWebEngineSettings::LinksIncludedInFocusChain;
    case QWebSettings::PrintElementBackgrounds:         return QWebEngineSettings::PrintElementBackgrounds;
    case QWebSettings::LocalStorageEnabled:             return QWebEngineSettings::LocalStorageEnabled;
    case QWebSettings::LocalContentCanAccessRemoteUrls: return QWebEngineSettings::LocalContentCanAccessRemoteUrls;
    case QWebSettings::DnsPrefetchEnabled:              return QWebEngineSettings::DnsPrefetchEnabled;
    case QWebSettings::XSSAuditingEnabled:              return QWebEngineSettings::XSSAuditingEnabled;
    case QWebSettings::SpatialNavigationEnabled:        return QWebEngineSettings::SpatialNavigationEnabled;
    case QWebSettings::LocalContentCanAccessFileUrls:   return QWebEngineSettings::LocalContentCanAccessFileUrls;
    default:
        *exists = false;
        return QWebEngineSettings::AutoLoadImages;
    }
}

QWebEngineSettings::FontFamily engineFont(QWebSettings::FontFamily family)
{
    switch (family) {
    case QWebSettings::FixedFont:     return QWebEngineSettings::FixedFont;
    case QWebSettings::SerifFont:     return QWebEngineSettings::SerifFont;
    case QWebSettings::SansSerifFont: return QWebEngineSettings::SansSerifFont;
    case QWebSettings::CursiveFont:   return QWebEngineSettings::CursiveFont;
    case QWebSettings::FantasyFont:   return QWebEngineSettings::FantasyFont;
    default:                          return QWebEngineSettings::StandardFont;
    }
}

} // namespace

QString qWebKitVersion()
{
    return QStringLiteral("534.34");
}

// ---------------------------------------------------------------------------
// QWebSettings

QWebSettings* QWebSettings::globalSettings()
{
    static QWebSettings* global = new QWebSettings(sharedProfile()->settings());
    return global;
}

void QWebSettings::setAttribute(WebAttribute attribute, bool on)
{
    m_attributes[attribute] = on;
    bool exists;
    const QWebEngineSettings::WebAttribute mapped = engineAttribute(attribute, &exists);
    if (exists && m_settings)
        m_settings->setAttribute(mapped, on);
}

bool QWebSettings::testAttribute(WebAttribute attribute) const
{
    bool exists;
    const QWebEngineSettings::WebAttribute mapped = engineAttribute(attribute, &exists);
    if (exists && m_settings)
        return m_settings->testAttribute(mapped);
    return m_attributes.value(attribute, false);
}

void QWebSettings::setFontFamily(FontFamily which, const QString& family)
{
    if (m_settings)
        m_settings->setFontFamily(engineFont(which), family);
}

// ---------------------------------------------------------------------------
// QWebPage

class QWebPage::Engine : public QWebEnginePage
{
public:
    Engine(QWebPage* owner) : QWebEnginePage(sharedProfile(), owner), m_owner(owner) {}

protected:
    QWebEnginePage* createWindow(WebWindowType type) override
    {
        QWebPage* created = m_owner->createWindow(type == WebDialog ? QWebPage::WebModalDialog
                                                                    : QWebPage::WebBrowserWindow);
        return created ? created->enginePage() : nullptr;
    }

    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override
    {
        QWebPage::NavigationType mapped = QWebPage::NavigationTypeOther;
        switch (type) {
        case NavigationTypeLinkClicked:  mapped = QWebPage::NavigationTypeLinkClicked; break;
        case NavigationTypeFormSubmitted: mapped = QWebPage::NavigationTypeFormSubmitted; break;
        case NavigationTypeBackForward:  mapped = QWebPage::NavigationTypeBackOrForward; break;
        case NavigationTypeReload:       mapped = QWebPage::NavigationTypeReload; break;
        default: break;
        }
        if (!m_owner->acceptNavigationRequest(isMainFrame ? m_owner->mainFrame() : nullptr,
                                              QNetworkRequest(url), mapped))
            return false;
        if (isMainFrame)
            m_owner->mainFrame()->prepareNewDocument();
        return true;
    }

    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel, const QString& message,
                                  int lineNumber, const QString& sourceId) override
    {
        m_owner->javaScriptConsoleMessage(message, lineNumber, sourceId);
    }

private:
    QWebPage* m_owner;
};

QWebPage::QWebPage(QObject* parent)
    : QObject(parent)
    , m_engine(nullptr)
    , m_view(new QWebEngineView)
    , m_frame(nullptr)
    , m_settings(nullptr)
    , m_viewportSize(1024, 768)
{
    m_engine = new Engine(this);
    m_frame = new QWebFrame(this);
    m_settings = new QWebSettings(m_engine->settings());

    m_view->setAttribute(Qt::WA_DontShowOnScreen);
    m_view->setPage(m_engine);
    m_view->resize(m_viewportSize);
    m_view->show();

    connect(m_engine, &QWebEnginePage::loadStarted, this, [this]() {
        followRenderSurface();
        Q_EMIT loadStarted();
    });
    // Every connection below goes through a lambda. Connected to a member
    // function instead, Qt's debug build dynamic_casts the receiver on each
    // emission, and WebAppMgr -- which subclasses this as SysMgrWebPage -- is
    // compiled with -fno-rtti: no type_info, and a segfault in loadProgress.
    connect(m_engine, &QWebEnginePage::loadProgress, this, [this](int progress) { Q_EMIT loadProgress(progress); });
    connect(m_engine, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        followRenderSurface();
        Q_EMIT loadFinished(ok);
    });
    connect(m_engine, &QWebEnginePage::geometryChangeRequested, this,
            [this](const QRect& geometry) { Q_EMIT geometryChangeRequested(geometry); });
    connect(m_engine, &QWebEnginePage::windowCloseRequested, this, [this]() { Q_EMIT windowCloseRequested(); });
    connect(m_engine, &QWebEnginePage::titleChanged, m_frame,
            [this](const QString& title) { Q_EMIT m_frame->titleChanged(title); });
    connect(m_engine, &QWebEnginePage::urlChanged, m_frame,
            [this](const QUrl& url) { Q_EMIT m_frame->urlChanged(url); });
    connect(m_engine, &QWebEnginePage::contentsSizeChanged, m_frame,
            [this](const QSizeF& size) { Q_EMIT m_frame->contentsSizeChanged(size.toSize()); });

    // Every script below runs in the child frames too. A QWebEngineScript is
    // main-frame-only unless it says otherwise, while QtWebKit cleared and
    // repopulated every frame's global object -- so HP's code assumes a frame
    // is a frame. The mail card loads ../accounts/ into an iframe, where the
    // account wizard asked for PalmServiceBridge and found nothing.

    // The bridge's JavaScript half, for documents loaded before any client
    // added an object.
    QWebEngineScript core;
    core.setName(kInjectedScriptName);
    core.setInjectionPoint(QWebEngineScript::DocumentCreation);
    core.setWorldId(QWebEngineScript::MainWorld);
    core.setRunsOnSubFrames(true);
    core.setSourceCode(QString::fromLatin1(kBridgeCore));
    m_engine->scripts().insert(core);

    // Separate from the bridge: prepareNewDocument() rewrites that one on every
    // document, and this has nothing to do with the objects it publishes.
    QWebEngineScript borderImage;
    borderImage.setName(kBorderImageScriptName);
    borderImage.setInjectionPoint(QWebEngineScript::DocumentCreation);
    borderImage.setWorldId(QWebEngineScript::MainWorld);
    borderImage.setRunsOnSubFrames(true);
    borderImage.setSourceCode(QString::fromLatin1(kBorderImageCompat));
    m_engine->scripts().insert(borderImage);

    // The prefixed events Chromium dropped; see above.
    QWebEngineScript prefixedEvents;
    prefixedEvents.setName(kPrefixedEventScriptName);
    prefixedEvents.setInjectionPoint(QWebEngineScript::DocumentCreation);
    prefixedEvents.setWorldId(QWebEngineScript::MainWorld);
    prefixedEvents.setRunsOnSubFrames(true);
    prefixedEvents.setSourceCode(QString::fromLatin1(kPrefixedEvents));
    m_engine->scripts().insert(prefixedEvents);

    // The WebView methods the mail app calls on a plain view; see above.
    QWebEngineScript appViewShims;
    appViewShims.setName(kAppViewShimScriptName);
    appViewShims.setInjectionPoint(QWebEngineScript::DocumentCreation);
    appViewShims.setWorldId(QWebEngineScript::MainWorld);
    appViewShims.setRunsOnSubFrames(true);
    appViewShims.setSourceCode(QString::fromLatin1(kAppViewShims));
    m_engine->scripts().insert(appViewShims);

    // The frame canceller Chromium dropped; see above.
    QWebEngineScript frameCancel;
    frameCancel.setName(kFrameCancelScriptName);
    frameCancel.setInjectionPoint(QWebEngineScript::DocumentCreation);
    frameCancel.setWorldId(QWebEngineScript::MainWorld);
    frameCancel.setRunsOnSubFrames(true);
    frameCancel.setSourceCode(QString::fromLatin1(kFrameCancel));
    m_engine->scripts().insert(frameCancel);

    // The browser's content area; see above.
    QWebEngineScript browserView;
    browserView.setName(kBrowserViewScriptName);
    browserView.setInjectionPoint(QWebEngineScript::DocumentCreation);
    browserView.setWorldId(QWebEngineScript::MainWorld);
    browserView.setRunsOnSubFrames(true);
    browserView.setSourceCode(QString::fromLatin1(kBrowserView));
    m_engine->scripts().insert(browserView);
}

QWebPage::~QWebPage()
{
    delete m_settings;
    delete m_view;   // owns nothing of ours; the engine page is our child
}

QWebEnginePage* QWebPage::enginePage() const
{
    return m_engine;
}

void QWebPage::embedPage(QWebPage* page, const QRect& rect)
{
    if (!page || page == this)
        return;

    // Already embedded: this is a move or a resize, which is what it will be
    // most of the time -- the hole travels with the page that owns it.
    for (int i = 0; i < m_embedded.size(); ++i) {
        if (m_embedded[i].page == page) {
            m_embedded[i].rect = rect;
            // An empty rect suspends the blit; it does not mean the page has
            // become nothing. Resizing its viewport to 0x0 would throw away the
            // layout it has to come back to.
            if (!rect.isEmpty())
                page->setViewportSize(rect.size());
            return;
        }
    }

    EmbeddedPage entry;
    entry.page = page;
    entry.rect = rect;
    if (!rect.isEmpty())
        page->setViewportSize(rect.size());

    // A frame of the embedded page is a frame of this one. The shell only ever
    // repaints what WindowedWebApp hands it, and that is the host page, so
    // without this the embedded page would paint into a buffer nobody asked
    // for again.
    entry.repaintLink = connect(page, &QWebPage::repaintRequested,
                                this, [this, page](const QRect&) {
        for (const EmbeddedPage& embedded : m_embedded) {
            if (embedded.page == page) {
                Q_EMIT repaintRequested(embedded.rect);
                return;
            }
        }
    });

    m_embedded.append(entry);
}

void QWebPage::removeEmbeddedPage(QWebPage* page)
{
    for (int i = m_embedded.size() - 1; i >= 0; --i) {
        if (m_embedded[i].page != page && !m_embedded[i].page.isNull())
            continue;
        disconnect(m_embedded[i].repaintLink);
        m_embedded.removeAt(i);
    }
}

void QWebPage::followRenderSurface()
{
    // QtWebEngine draws a page through a QQuickWidget that it creates, and may
    // replace, as documents load. Its scene graph renders once per new frame,
    // which is when QtWebKit would have asked for a repaint.
    QQuickWidget* surface = qobject_cast<QQuickWidget*>(m_view->focusProxy());
    if (!surface || !surface->quickWindow() || m_renderSurface == surface->quickWindow())
        return;
    m_renderSurface = surface->quickWindow();
    connect(surface->quickWindow(), &QQuickWindow::afterRendering, this, [this]() {
        Q_EMIT repaintRequested(QRect(QPoint(0, 0), m_viewportSize));
    }, Qt::QueuedConnection);
}

void QWebPage::triggerAction(WebAction action, bool checked)
{
    switch (action) {
    case Cut:       m_engine->triggerAction(QWebEnginePage::Cut, checked); break;
    case Copy:      m_engine->triggerAction(QWebEnginePage::Copy, checked); break;
    case Paste:     m_engine->triggerAction(QWebEnginePage::Paste, checked); break;
    case Undo:      m_engine->triggerAction(QWebEnginePage::Undo, checked); break;
    case Redo:      m_engine->triggerAction(QWebEnginePage::Redo, checked); break;
    case SelectAll: m_engine->triggerAction(QWebEnginePage::SelectAll, checked); break;
    default: break;
    }
}

void QWebPage::setViewportSize(const QSize& size)
{
    if (size.isEmpty() || size == m_viewportSize)
        return;
    m_viewportSize = size;
    m_view->resize(size);
    followRenderSurface();
}

QWebPage::ViewportAttributes QWebPage::viewportAttributesForSize(const QSize& availableSize) const
{
    ViewportAttributes attributes;
    attributes.m_size = QSizeF(availableSize);
    return attributes;
}

void QWebPage::setPalette(const QPalette& palette)
{
    m_palette = palette;
    if (palette.brush(QPalette::Base).color().alpha() == 0)
        m_engine->setBackgroundColor(Qt::transparent);
}

// An event that carries a position belongs to whatever is painted where it
// landed. WebAppMgr turns the shell's touches into QMouseEvents in the card's
// coordinates and gives them to the host page (WindowedWebApp.cpp:405), which
// knows only its own widget -- so with a page embedded in it, the browser drew
// its content and nothing in it could be clicked or scrolled: every touch was
// delivered to the page holding the hole, where there is only an empty div.
//
// Keyboard events are deliberately not routed here. They follow focus rather
// than a position, and sending them to an embedded page would take typing away
// from the address bar, which is the app's own field. That needs a focus model
// of its own.
bool QWebPage::deliverToEmbedded(QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
        break;
    default:
        return false;
    }

    const QPointF where = static_cast<QSinglePointEvent*>(event)->position();

    // Last registered is topmost, so it is asked first.
    for (int i = m_embedded.size() - 1; i >= 0; --i) {
        const EmbeddedPage& embedded = m_embedded[i];
        if (embedded.page.isNull() || embedded.rect.isEmpty())
            continue;
        if (!embedded.rect.contains(where.toPoint()))
            continue;

        const QPointF local = where - QPointF(embedded.rect.topLeft());
        QWidget* target = embedded.page->m_view->focusProxy()
                        ? embedded.page->m_view->focusProxy()
                        : embedded.page->m_view;

        bool handled = false;
        if (event->type() == QEvent::Wheel) {
            QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
            QWheelEvent translated(local, local, wheel->pixelDelta(), wheel->angleDelta(),
                                   wheel->buttons(), wheel->modifiers(),
                                   wheel->phase(), wheel->inverted());
            handled = QCoreApplication::sendEvent(target, &translated);
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            // Dragging scrolls, with the content following the finger. Nothing
            // in luna-sysmgr or webappmanager handles a wheel -- webOS scrolled
            // by gesture, and its event catalogue has no scroll member -- so
            // this is the only scrolling an embedded page can be given without
            // changing HP's input path. The cost is that a drag no longer
            // selects text there, or drags a scrollbar: same gesture, and on a
            // touchscreen it belongs to scrolling.
            //
            // tests/embedded-scroll checks that a drag scrolls, and deliberately
            // does not check how far. An earlier version asserted 80 pixels of
            // drag should move the page 80, measured 4 in the harness, and that
            // number was used to junk this code -- which was working on the real
            // shell all along. A ten-step synthetic drag is not a real one.
            const QPointF delta = where - m_dragAt;
            m_dragAt = where;
            const QPoint pixels(int(delta.x()), int(delta.y()));
            QWheelEvent scroll(local, local, pixels, pixels,
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            handled = QCoreApplication::sendEvent(target, &scroll);
        } else {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            QMouseEvent translated(mouse->type(), local, local,
                                   mouse->button(), mouse->buttons(), mouse->modifiers());
            handled = QCoreApplication::sendEvent(target, &translated);
        }

        // A press is also what decides where typing goes from now on. Without
        // this the embedded widget takes Qt's focus on the first click and the
        // app's address bar can never be typed into again; with the keyboard
        // pinned to the host instead, a field inside the page could never be
        // typed into. Whichever was pressed last owns it, as in any browser.
        if (event->type() == QEvent::MouseButtonPress) {
            m_keyboardOwner = embedded.page;
            target->setFocus(Qt::MouseFocusReason);
            m_dragAt = where;
            m_dragging = true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
        }

        return handled;
    }

    // Pressed somewhere that is not an embedded page: the host takes the
    // keyboard back, which is what makes the address bar usable again after a
    // click in the content.
    if (event->type() == QEvent::MouseButtonPress) {
        m_keyboardOwner.clear();
        if (QWidget* host = m_view->focusProxy() ? m_view->focusProxy() : m_view)
            host->setFocus(Qt::MouseFocusReason);
    }

    return false;
}

bool QWebPage::event(QEvent* event)
{
    if (deliverToEmbedded(event))
        return true;

    // Typing goes to whatever was pressed last. WebAppMgr sends every key to
    // the host page, so an embedded page would never see one otherwise.
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::InputMethod:
        if (!m_keyboardOwner.isNull()) {
            QWidget* target = m_keyboardOwner->m_view->focusProxy()
                            ? m_keyboardOwner->m_view->focusProxy()
                            : m_keyboardOwner->m_view;
            return QCoreApplication::sendEvent(target, event);
        }
        break;
    default:
        break;
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::InputMethod:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        QWidget* target = m_view->focusProxy() ? m_view->focusProxy() : m_view;
        return QCoreApplication::sendEvent(target, event);
    }
    default:
        return QObject::event(event);
    }
}

QWebPage* QWebPage::createWindow(WebWindowType)
{
    return nullptr;
}

bool QWebPage::acceptNavigationRequest(QWebFrame*, const QNetworkRequest&, NavigationType)
{
    return true;
}

void QWebPage::javaScriptConsoleMessage(const QString& message, int lineNumber, const QString& sourceId)
{
    qInfo().noquote() << QString("JS: %1:%2: %3").arg(sourceId).arg(lineNumber).arg(message);
}

// ---------------------------------------------------------------------------
// QWebFrame

QWebFrame::QWebFrame(QWebPage* page)
    : QObject(page)
    , m_page(page)
{
}

QUrl QWebFrame::url() const
{
    return m_page->m_engine->url();
}

void QWebFrame::load(const QUrl& url)
{
    m_page->m_engine->load(url);
}

void QWebFrame::setHtml(const QString& html, const QUrl& baseUrl)
{
    m_page->m_engine->setHtml(html, baseUrl);
}

QString QWebFrame::title() const
{
    return m_page->m_engine->title();
}

void QWebFrame::render(QPainter* painter, RenderLayer, const QRegion& clip)
{
    const QPixmap frame = m_page->m_view->grab();
    painter->save();
    if (!clip.isEmpty())
        painter->setClipRegion(clip, Qt::IntersectClip);
    painter->drawPixmap(0, 0, frame);

    // Then the pages embedded in this one, each over its own hole. This is what
    // BrowserAdapter did with the buffer BrowserServer had filled, without the
    // plugin, the second process, the shared buffers or the semaphore: both
    // engines are ours and in this process. tests/embedded-view checks that
    // what the embedded page painted lands inside the host's pixels, in the
    // right place and nowhere else.
    for (const QWebPage::EmbeddedPage& embedded : m_page->m_embedded) {
        if (embedded.page.isNull() || embedded.rect.isEmpty())
            continue;
        const QPixmap content = embedded.page->m_view->grab();
        if (content.isNull())
            continue;
        painter->save();
        painter->setClipRect(embedded.rect, Qt::IntersectClip);
        painter->drawPixmap(embedded.rect.topLeft(), content);
        painter->restore();
    }

    painter->restore();
}

void QWebFrame::prepareNewDocument()
{
    m_collecting = true;
    m_collected.clear();
    Q_EMIT javaScriptWindowObjectCleared();
    m_collecting = false;

    QWebEngineScriptCollection& scripts = m_page->m_engine->scripts();
    for (const QWebEngineScript& old : scripts.find(kInjectedScriptName))
        scripts.remove(old);
    QWebEngineScript script;
    script.setName(kInjectedScriptName);
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    // The objects a client published belong to every frame, as they did under
    // QtWebKit; see the collection built in QWebPage's constructor.
    script.setRunsOnSubFrames(true);
    script.setSourceCode(QString::fromLatin1(kBridgeCore) + m_collected);
    scripts.insert(script);
}

void QWebFrame::addToJavaScriptWindowObject(const QString& name, QObject* object)
{
    if (!object)
        return;
    const int id = publishObject(object, m_page->m_engine);
    const QString assignment = QString("window[%1] = window.__webosBridge.proxy(%2, %3);\n")
        .arg(QString::fromUtf8(QJsonDocument(QJsonArray{name}).toJson(QJsonDocument::Compact)).mid(1).chopped(1))
        .arg(id)
        .arg(QString::fromUtf8(QJsonDocument(describe(object->metaObject())).toJson(QJsonDocument::Compact)));
    if (m_collecting)
        m_collected += assignment;
    else
        m_page->m_engine->runJavaScript(QString::fromLatin1(kBridgeCore) + assignment);
}

QVariant QWebFrame::evaluateAndWait(const QString& script) const
{
    QVariant result;
    bool done = false;
    m_page->m_engine->runJavaScript(script, [&result, &done](const QVariant& value) {
        result = value;
        done = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 5000) {
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    return result;
}

QVariant QWebFrame::evaluateJavaScript(const QString& script)
{
    if (m_collecting) {
        // Part of the new document's first script: QtWebKit ran it against the
        // fresh global object before any of the page's own code.
        m_collected += script + QStringLiteral(";\n");
        return QVariant();
    }
    return evaluateAndWait(script);
}

QWebElement QWebFrame::findFirstElement(const QString& selectorQuery) const
{
    const QString query = QString::fromUtf8(QJsonDocument(QJsonArray{selectorQuery}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    const QVariantMap found = evaluateAndWait(QString(R"JS((function () {
        var e = document.querySelector(%1);
        if (!e) return null;
        var r = e.getBoundingClientRect(), attrs = {};
        for (var i = 0; i < e.attributes.length; ++i) attrs[e.attributes[i].name] = e.attributes[i].value;
        return { tag: e.tagName, attrs: attrs, rect: [r.left, r.top, r.width, r.height] };
    })())JS").arg(query)).toMap();

    QWebElement element;
    if (found.isEmpty())
        return element;
    element.m_null = false;
    element.m_tagName = found.value("tag").toString();
    const QVariantMap attrs = found.value("attrs").toMap();
    for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it)
        element.m_attributes.insert(it.key(), it.value().toString());
    const QVariantList rect = found.value("rect").toList();
    if (rect.size() == 4)
        element.m_geometry = QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble()).toRect();
    return element;
}

QWebHitTestResult QWebFrame::hitTestContent(const QPoint& pos) const
{
    const QVariantMap found = evaluateAndWait(QString(R"JS((function () {
        var e = document.elementFromPoint(%1, %2);
        if (!e) return null;
        var r = e.getBoundingClientRect(), attrs = {};
        for (var i = 0; i < e.attributes.length; ++i) attrs[e.attributes[i].name] = e.attributes[i].value;
        var editable = e.isContentEditable || e.tagName === "TEXTAREA"
            || (e.tagName === "INPUT" && !/^(button|checkbox|radio|submit|reset|image|file|hidden)$/i.test(e.type));
        return { tag: e.tagName, attrs: attrs, rect: [r.left, r.top, r.width, r.height], editable: editable };
    })())JS").arg(pos.x()).arg(pos.y())).toMap();

    QWebHitTestResult result;
    if (found.isEmpty())
        return result;
    result.m_editable = found.value("editable").toBool();
    result.m_element.m_null = false;
    result.m_element.m_tagName = found.value("tag").toString();
    const QVariantMap attrs = found.value("attrs").toMap();
    for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it)
        result.m_element.m_attributes.insert(it.key(), it.value().toString());
    const QVariantList rect = found.value("rect").toList();
    if (rect.size() == 4)
        result.m_element.m_geometry = QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble()).toRect();
    return result;
}

void QWebFrame::setScrollBarPolicy(Qt::Orientation, Qt::ScrollBarPolicy policy)
{
    // QtWebEngine has one switch for both orientations.
    m_page->m_engine->settings()->setAttribute(QWebEngineSettings::ShowScrollBars,
                                               policy != Qt::ScrollBarAlwaysOff);
}

// ---------------------------------------------------------------------------
// QGraphicsWebView

QGraphicsWebView::QGraphicsWebView(QGraphicsItem* parent)
    : QGraphicsWidget(parent)
{
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
}

QGraphicsWebView::~QGraphicsWebView() = default;

void QGraphicsWebView::setPage(QWebPage* page)
{
    if (m_page == page)
        return;
    if (m_page)
        disconnect(m_page, nullptr, this, nullptr);
    m_page = page;
    if (!m_page)
        return;
    m_page->setViewportSize(size().toSize());
    connect(m_page, &QWebPage::repaintRequested, this, [this](const QRect& rect) { update(QRectF(rect)); });
}

void QGraphicsWebView::setGeometry(const QRectF& rect)
{
    QGraphicsWidget::setGeometry(rect);
    if (m_page)
        m_page->setViewportSize(rect.size().toSize());
}

void QGraphicsWebView::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    if (!m_page)
        return;
    const QRegion clip = option ? QRegion(option->exposedRect.toAlignedRect()) : QRegion();
    m_page->mainFrame()->render(painter, QWebFrame::ContentsLayer, clip);
}
