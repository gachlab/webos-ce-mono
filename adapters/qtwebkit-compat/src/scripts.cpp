#include "detail.h"

namespace qtwebkit_compat_detail {

const char kInjectedScriptName[] = "webos-document-creation";
const char kBorderImageScriptName[] = "webos-border-image";
const char kPrefixedEventScriptName[] = "webos-prefixed-events";
const char kAppViewShimScriptName[] = "webos-app-view-shims";
const char kFrameCancelScriptName[] = "webos-frame-cancel";
const char kFlexWidthScriptName[] = "webos-flex-width";
const char kEnyoWheelScriptName[] = "webos-enyo-wheel";
const char kNumberInputScriptName[] = "webos-number-inputs";
const char kWindowOpenScriptName[] = "webos-window-open";
const char kRemoteRequestScriptName[] = "webos-remote-requests";

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
            // @media and @supports are grouping rules: they carry no style of
            // their own and their children are NOT in this list. Skipping them
            // on the strength of a missing .style, which is what this loop did,
            // left every rule inside them unpatched -- and enyo puts its whole
            // radio and tab button theme inside
            // @media (-webkit-max-device-pixel-ratio: ...).
            //
            // The clock's toolbar showed the cost: its two buttons collapsed
            // onto their bare icons, touching, with no button box at all.
            // .enyo-radiobutton carries "border-width: 0px 16px" and the
            // -webkit-border-image sits on .enyo-radiobutton.enyo-first inside
            // the media block, so the element never got a border-style and its
            // 16px sides computed to 0. Patching either rule is enough --
            // border-style applies to the ELEMENT, not to the rule that set it.
            // Recurse into a grouping rule IN ADDITION to patching this one,
            // never instead of it. A first attempt did "if (cssRules) { ...;
            // continue; }" and skipped every ordinary rule too: an empty
            // CSSRuleList is still an object, so the guard fired on rules that
            // had a perfectly good .style and the entire sheet went unpatched.
            // Measured -- the plain "element with a border image" case dropped
            // from 130 to 100 and no resize was dispatched at all, which is the
            // signature of the script doing nothing rather than doing it wrong.
            if (rules[r].cssRules)
                changed += patchSheet(rules[r]);
            var style = rules[r].style;
            if (!style)
                continue;
            var image = style.getPropertyValue("-webkit-border-image")
                     || style.getPropertyValue("border-image")
                     || style.getPropertyValue("border-image-source");
            if (!image || image === "none")
                continue;
            // "initial" is not a declaration, but it reads back as one.
            //
            // `border: 12px` is valid CSS: the shorthand takes any subset, sets
            // border-width, and resets style and colour to their INITIAL
            // values. So border-style reads back the literal string "initial",
            // which is truthy, and this guard skipped the rule -- leaving the
            // element a 12px border-width it could never paint, which is the
            // same nothing as having no border at all.
            //
            // Contacts shows the cost. `.edit .field-button` came out 14x14
            // instead of 38x38 (14 + 12 + 12): its 32px icon, positioned with
            // margin:-9px to sit over the border box, ended up 9px outside its
            // own parent and 13px above the row's centre, so the star and the
            // info button rode high over the Name field.
            //
            // "none" is a real declaration and is deliberately NOT included.
            // An author writing `border: none` means it. Treating it as unset
            // was tried and is worse than the bug: border-width then falls back
            // to `medium`, so every .enyo-input-input and .enyo-richtext in the
            // tree grew a 3px border, and every text field in every app came up
            // inside a black box. Measured, and visible in one screenshot.
            //
            // Nor is it enough to require that the same rule declare a
            // border-width. Only 44 of 171 border-image rules do; the width
            // usually arrives from another rule for the same element, and
            // .enyo-button and .enyo-radiobutton -- the controls this whole
            // shim was written for -- are among the 127 that would have been
            // dropped.
            var declared = style.getPropertyValue("border-style");
            if (declared && declared !== "initial")
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
// The width enyo's flex layout writes and Chromium never gives back.
//
// enyo is built on the 2009 flexbox. FlexLayout.flowExtent redefines flex to
// mean "be exactly the left over space" rather than "natural size plus the
// left over space", and implements that by writing an inline width of 0 next
// to the flex:
//
//     s[this.prefix + "-box-flex"] = f;
//     if (f) { if (!s[inExtent]) s[inExtent] = "0px"; }
//
// In the WebKit webOS shipped, a -webkit-box child with flex:1 and width:0
// still grew to its share of the line. Chromium's legacy -webkit-box gives it
// nothing, so the element stays 0 wide and its content either disappears or
// paints outside the parent that was supposed to hold it.
//
// Contacts is where it shows. Measured in the running app: the type pickers'
// labels -- MOBILE, HOME, .MAC -- sat in containers 0px wide, so the text was
// simply not painted; the account selector at the top right came out 50px
// wide with its content escaping 28px past it; and the open dropdown's option
// read "HP" because the caption had clientWidth 34 against scrollWidth 290.
// One mechanism, three symptoms.
//
// The sweep is deliberately narrow. Clearing every inline width:0px would
// touch 51 nodes in that page; clearing only the ones whose content is
// demonstrably starved touches 15 and produces the identical result -- 46
// changed boxes either way, because the other 36 were not holding anything
// back. Across the other apps running at the time -- Calendar, Mail, Just
// Type, the status bar -- the narrow rule changes nothing at all, which is the
// point: it repairs a broken box, it does not re-lay out the framework.
//
// Only a zero width FlexLayout wrote, which it always writes next to a flex.
// A zero width alone is someone clipping on purpose: the dashboard hides the
// notifications under the top one in boxes 0px wide with overflow hidden,
// and clearing those drew all three on top of each other.
//
// It runs again on mutation because the popup lists are built when they are
// opened, and re-running is safe: enyo writes these styles when it renders and
// nothing re-applies them afterwards, verified by calling resized() on 564
// controls and finding the cleared widths still cleared.
const char kFlexWidthCompat[] = R"JS(
(function () {
    if (window.__webosFlexWidth)
        return;
    window.__webosFlexWidth = true;

    // Flexed means: FlexLayout wrote the zero width, next to the flex.
    function flexed(n) {
        return parseFloat(n.style.getPropertyValue("-webkit-box-flex")) > 0;
    }

    // Starved means: the element carries the inline zero width AND something
    // is actually being cut off by it. scrollWidth past clientWidth catches a
    // clipped caption; the second test catches content that measures nothing
    // at all, which is what an invisible label looks like.
    function starved(n) {
        if (n.scrollWidth > n.clientWidth + 1)
            return true;
        return (n.textContent || "").trim().length > 0 &&
               n.getBoundingClientRect().width < 1;
    }

    var scheduled = false;

    function sweep() {
        scheduled = false;
        var nodes = document.querySelectorAll(
            '[style*="width:0px"],[style*="width: 0px"]');
        for (var i = 0; i < nodes.length; i++)
            if (flexed(nodes[i]) && starved(nodes[i]))
                nodes[i].style.removeProperty("width");
    }

    // Coalesce: enyo renders in bursts, and one pass after the burst is both
    // cheaper and more accurate than one per node -- the measurements above
    // only mean anything once the surrounding layout has settled.
    function schedule() {
        if (scheduled)
            return;
        scheduled = true;
        window.setTimeout(sweep, 0);
    }

    document.addEventListener("DOMContentLoaded", schedule);
    window.addEventListener("load", schedule);
    window.addEventListener("resize", schedule);

    if (window.MutationObserver) {
        // childList only, never attributes: the sweep itself edits style
        // attributes, and observing those would have it wake itself up.
        new MutationObserver(function (records) {
            for (var i = 0; i < records.length; i++) {
                if (records[i].addedNodes.length) {
                    schedule();
                    return;
                }
            }
        }).observe(document, { childList: true, subtree: true });
    }
})();
)JS";

// ---------------------------------------------------------------------------
// The JavaScript side of the object bridge. Runs once per document, first.

// The wheel event enyo is listening for, which Chromium stopped sending.
//
// enyo's Dispatcher.js registers "mousewheel" -- the legacy name -- and
// ScrollStrategy.mousewheel reads wheelDeltaY out of it. Chromium dispatches
// the standard "wheel" and never the alias: MEASURED in the running shell as
// 558 wheel events against 0 mousewheel. And nothing else can scroll these
// lists either, because .enyo-scroller is overflow:hidden and enyo moves its
// content with translate3d from a JavaScript simulation. So the wheel arrived
// at HP's apps and did nothing at all, while the browser scrolled perfectly --
// what scrolls there is an ordinary Chromium document.
//
// Scoped to targets inside an .enyo-scroller, and that is measured rather than
// cautious: a census of the running pages found the browser's embedded content
// with 0 enyo scrollers and its chrome with 2 that hold no content, so this
// cannot fire where Chromium is already scrolling and cannot double-scroll
// anything.
//
// wheelDeltaY is set explicitly instead of being left to the constructor. A
// constructed WheelEvent does expose the attribute, but derives it as +deltaY
// -- MEASURED: {deltaY: 100} yields wheelDeltaY 100, where a real event
// reports the negation. Handed to enyo unchanged it would scroll the list
// backwards, since ScrollStrategy adds the value straight to its position.
//
// A real event does carry one, and carries it correctly, so that is what is
// passed through. MEASURED on the events this port delivers, in both contexts:
// deltaY 63, 135 and 145 inside an enyo scroller arrived as wheelDeltaY -125,
// -270 and -290, and deltaY 78, 150 and 242.5 on the browser's page as -156,
// -300 and -484. Negated, and a factor of two rather than the three of Blink's
// older convention -- which is why the fallback below uses the number that was
// measured instead of the one that is usually quoted.
//
// The same samples confirm the scoping: closest(".enyo-scroller") was true for
// every sample taken in the calendar and false for every one taken on the
// browser's content.
const char kEnyoWheelCompat[] = R"JS(
(function () {
    if (window.__webosEnyoWheel)
        return;
    window.__webosEnyoWheel = true;

    document.addEventListener("wheel", function (e) {
        var target = e.target;
        if (!target || !target.closest || !target.closest(".enyo-scroller"))
            return;

        // What Blink already computed for this event, which on a real one is
        // the negated delta and is exactly what enyo expects. The fallback is
        // only for an event that arrives without it; its factor is the one
        // measured here rather than the 3 of Blink's historical convention.
        var legacyY = e.wheelDeltaY;
        if (typeof legacyY !== "number" || !isFinite(legacyY) || legacyY === 0)
            legacyY = -e.deltaY * 2;
        var legacyX = e.wheelDeltaX;
        if (typeof legacyX !== "number" || !isFinite(legacyX))
            legacyX = -e.deltaX * 2;

        // A "mousewheel" of our own. It does not re-enter this listener --
        // that one is bound to "wheel" -- and it bubbles, which is how it
        // reaches the document listener enyo installed.
        var synth = new WheelEvent("mousewheel", {
            bubbles: true,
            cancelable: true,
            clientX: e.clientX,
            clientY: e.clientY,
            deltaX: e.deltaX,
            deltaY: e.deltaY
        });
        Object.defineProperty(synth, "wheelDeltaY", { value: legacyY, configurable: true });
        Object.defineProperty(synth, "wheelDeltaX", { value: legacyX, configurable: true });
        Object.defineProperty(synth, "wheelDelta",  { value: legacyY, configurable: true });

        target.dispatchEvent(synth);
    }, true);
})();
)JS";

// ---------------------------------------------------------------------------
// Number inputs that take text, as they did in webOS's WebKit.
//
// HP's Wi-Fi settings declare the address fields -- IP, subnet, gateway, DNS --
// as <input type="number">, so the keyboard offers digits, and write addresses
// like "10.20.30.99" into them. The WebKit webOS shipped kept whatever text it
// was given. Chromium sanitizes a number input's value and drops anything that
// is not a floating-point number -- "The specified value "10.20.30.99" cannot
// be parsed" -- so the connected network's address showed as an empty field,
// and an address typed by hand would read back as "".
//
// A number input that is handed text, or that takes focus, becomes a text
// input with inputmode="decimal": the same digits-first keyboard, and no
// sanitizing. The value setter is where it has to happen: enyo renders the
// field and sets its value in the same task, so an observer would run after
// the value was already dropped.
const char kNumberInputs[] = R"JS(
(function () {
    if (window.__webosNumberInputs)
        return;
    window.__webosNumberInputs = true;
    var proto = window.HTMLInputElement && HTMLInputElement.prototype;
    var desc = proto && Object.getOwnPropertyDescriptor(proto, "value");
    if (!desc || !desc.set)
        return;

    function asText(input) {
        if (input.type !== "number")
            return;
        input.setAttribute("inputmode", "decimal");
        input.type = "text";
    }

    Object.defineProperty(proto, "value", {
        configurable: true,
        enumerable: desc.enumerable,
        get: desc.get,
        set: function (v) {
            if (this.type === "number" && v !== null && v !== undefined && v !== "" &&
                    !/^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$/.test(String(v)))
                asText(this);
            desc.set.call(this, v);
        }
    });

    document.addEventListener("focusin", function (e) {
        if (e.target instanceof HTMLInputElement)
            asText(e.target);
    }, true);
})();
)JS";

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

    // Anything the app draws over the hole, as rects in the page's
    // coordinates, clipped to the hole.
    //
    // The blit goes on top of everything the page painted, so whatever the app
    // opens across the content area would end up underneath it. Measured on
    // the running browser: its action bar menu is an absolutely positioned
    // "enyo-popup enyo-popup-menu launch-popup" at [727, 30, 153, 164], z-index
    // 123, over a hole starting at y 54. The host leaves these rects out of the
    // blit, so the menu shows over the page -- rather than the whole page
    // going blank while a menu is open, as it first did.
    //
    // Full-page containers are not overlays: the hole's own ancestors are
    // absolute and as large as the view.
    // How far into a border image its opaque part starts, per image, in the
    // image's pixels. enyo's menus draw their panel and their shadow with one
    // (Onyx's menu-background.png: the panel starts 6 px in at the sides and
    // 9 px up from the bottom), and cutting the shadow out too left a white
    // band around the menu where the page should have shown.
    var opaque = {};
    var OPAQUE_ALPHA = 200;

    function measureImage(image) {
        var canvas = document.createElement("canvas");
        canvas.width = image.width;
        canvas.height = image.height;
        var context = canvas.getContext("2d");
        context.drawImage(image, 0, 0);
        var data = context.getImageData(0, 0, image.width, image.height).data;
        var alpha = function (x, y) { return data[(y * image.width + x) * 4 + 3]; };
        var row = Math.floor(image.height / 2);
        var column = Math.floor(image.width / 2);
        var edge = function (length, at) {
            for (var i = 0; i < length; i++)
                if (at(i) >= OPAQUE_ALPHA)
                    return i;
            return 0;
        };
        return {
            left: edge(image.width, function (i) { return alpha(i, row); }),
            right: edge(image.width, function (i) { return alpha(image.width - 1 - i, row); }),
            top: edge(image.height, function (i) { return alpha(column, i); }),
            bottom: edge(image.height, function (i) { return alpha(column, image.height - 1 - i); })
        };
    }

    // The element's rect less its border image's see-through margin, or the
    // rect itself when there is none (or it is not known yet).
    function opaqueRect(style, r) {
        var match = /^url\("?(.*?)"?\)$/.exec(style.borderImageSource || "");
        var slice = parseFloat(style.borderImageSlice);
        if (!match || !(slice > 0))
            return r;
        var src = match[1];
        if (!(src in opaque)) {
            opaque[src] = null;
            var image = new Image();
            image.onload = function () {
                try {
                    opaque[src] = measureImage(image);
                } catch (e) {
                    opaque[src] = false;
                }
                remeasureAll();
            };
            image.onerror = function () { opaque[src] = false; };
            image.src = src;
        }
        var inset = opaque[src];
        if (!inset)
            return r;
        var side = function (name, pixels) {
            return Math.min(pixels, slice) * (parseFloat(style["border" + name + "Width"]) || 0) / slice;
        };
        return {
            left: r.left + side("Left", inset.left),
            top: r.top + side("Top", inset.top),
            right: r.right - side("Right", inset.right),
            bottom: r.bottom - side("Bottom", inset.bottom)
        };
    }

    function coverings(node, b) {
        var found = [];
        var sx = window.pageXOffset || 0;
        var sy = window.pageYOffset || 0;
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
            var seen = opaqueRect(style, r);
            var left = Math.max(Math.floor(seen.left + sx), b.x);
            var top = Math.max(Math.floor(seen.top + sy), b.y);
            var right = Math.min(Math.ceil(seen.right + sx), b.x + b.w);
            var bottom = Math.min(Math.ceil(seen.bottom + sy), b.y + b.h);
            if (right <= left || bottom <= top)
                continue;
            found.push([left, top, right - left, bottom - top]);
        }
        return found;
    }

    function setCutouts(control, rects) {
        var key = JSON.stringify(rects);
        if (control.__webosCutouts === key)
            return;
        control.__webosCutouts = key;
        if (control.__webosView.setCutouts)
            control.__webosView.setCutouts(rects);
    }

    // While a hole cannot be painted, measure it again for a while: the
    // change that frees it may come with no event of its own.
    var RETRY_MS = 100;
    var RETRIES = 40;

    function retry(control, attempt) {
        clearTimeout(control.__webosRetry);
        if (attempt < RETRIES)
            control.__webosRetry = setTimeout(function () { sendGeometry(control, attempt + 1); }, RETRY_MS);
    }

    function sendGeometry(control, attempt) {
        var node = control.hasNode && control.hasNode();
        if (!node || !control.__webosView)
            return;
        attempt = attempt || 0;
        var b = boundsOf(node);
        if (b.w <= 0 || b.h <= 0) {
            // Nothing to paint into. Either the pane that owns this view has
            // not revealed it -- the browser opens on its start page, and enyo
            // keeps the others at display:none -- or it just has, and the
            // layout is not settled in the same turn. Stop blitting meanwhile,
            // which is also what keeps a backgrounded tab from painting over
            // the one in front, and measure again for a moment.
            suspend(control);
            retry(control, attempt);
            return;
        }
        var over = coverings(node, b);
        setCutouts(control, over);
        setRect(control, b);
        if (over.length > 0) {
            // What covers it may go away with no event of its own. Found live:
            // back from the Preferences, the browser's view is shown while
            // they still cover it, and they leave without the view changing
            // size -- so nothing measured it again.
            retry(control, attempt);
        } else {
            clearTimeout(control.__webosRetry);
        }
    }

    function remeasureAll() {
        for (var i = 0; i < holes.length; i++)
            sendGeometry(holes[i]);
    }

    // The verbs isis-browser actually sends. Everything else the control emits
    // on its way up -- pageFocused, addUrlRedirect, handleFlick and the rest --
    // is taken and dropped: either the engine already does it, or nothing
    // depends on it yet.
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
        // The browser's preferences.
        case "setEnableJavaScript": view.setEnableJavaScript(args[0] !== false); break;
        case "setBlockPopups":      view.setBlockPopups(args[0] !== false); break;
        case "setAcceptCookies":    view.setAcceptCookies(args[0] !== false); break;
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

        // A hole hidden by its app says nothing: enyo's Pane shows another
        // view by setting display:none on the browser's, and nothing calls
        // back. Found live: the Preferences opened over a loaded page and the
        // page went on being painted over them. The box's own size changes,
        // though, to nothing and back, and that is what this watches.
        if (window.ResizeObserver) {
            new ResizeObserver(function () { sendGeometry(control); }).observe(node);
        }

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
        // A file the page would download: the plugin's mimeNotSupported, which
        // the app turns into a com.palm.downloadmanager download.
        if (view.fileRequested) {
            view.fileRequested.connect(function (mimeType, url) {
                if (control.mimeNotSupported)
                    control.mimeNotSupported(mimeType, url);
            });
        }
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

// window.open()'s third argument, which QtWebEngine never hands to
// createWindow(). Palm's QtWebKit kept it for the new page, and WebAppMgr reads
// its "attributes=" part to decide what a window is: enyo and Mojo open every
// dashboard, alert and child card with it. Without it every window became a
// card.
//
// So the string goes to the C++ side, synchronously, just before the call that
// creates the window. %1 is the page's number; frames share their page's.
const char kWindowOpen[] = R"JS(
(function (page) {
    var open = window.open;
    if (typeof open !== "function" || open.__webosPage !== undefined)
        return;
    var wrapped = function (url, name, features) {
        // Sent even when empty, so an earlier call's string is never taken
        // for this one's.
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "webos-bridge:///window/" + page + "?a="
                 + encodeURIComponent(JSON.stringify([typeof features === "string" ? features : ""])), false);
        xhr.send();
        return open.apply(this, arguments);
    };
    wrapped.__webosPage = page;
    window.open = wrapped;
})(%1);
)JS";

// Requests from an app's own documents to the network.
//
// HP's apps are file:// documents that call web services with XMLHttpRequest
// -- Just Type asks the default search engine for suggestions that way -- and
// Palm's QtWebKit let them (LocalContentCanAccessRemoteUrls, which WebAppMgr
// still sets). Chromium does not: it applies CORS to a file:// origin whatever
// that setting says, and a service that sends no CORS headers is refused.
// MEASURED in the running launcher: "Access to XMLHttpRequest at
// 'https://suggestqueries.google.com/...' from origin 'file://' has been
// blocked by CORS policy", with or without a preflight.
//
// Turning web security off is not an option: the same process shows the
// browser's pages. So only file:// documents are changed, and only their
// requests to http(s): those go through webos-bridge, whose handler makes the
// request itself (see proxyRequest). Web pages keep the web's rules.
const char kRemoteRequests[] = R"JS(
(function () {
    if (window.__webosRemoteRequests || location.protocol !== "file:")
        return;
    window.__webosRemoteRequests = true;

    function proxied(url) {
        var absolute;
        try {
            absolute = new URL(String(url), location.href);
        } catch (e) {
            return url;
        }
        if (absolute.protocol !== "http:" && absolute.protocol !== "https:")
            return url;
        return "webos-bridge:///fetch?u=" + encodeURIComponent(absolute.href);
    }

    var open = XMLHttpRequest.prototype.open;
    XMLHttpRequest.prototype.open = function (method, url) {
        var args = Array.prototype.slice.call(arguments);
        args[1] = proxied(url);
        return open.apply(this, args);
    };

    if (window.fetch) {
        var fetch = window.fetch;
        window.fetch = function (input, init) {
            if (typeof input === "string" || input instanceof URL)
                return fetch.call(this, proxied(input), init);
            return fetch.call(this, input, init);
        };
    }
})();
)JS";

// The last features string each page announced, by page number, until the

} // namespace qtwebkit_compat_detail
