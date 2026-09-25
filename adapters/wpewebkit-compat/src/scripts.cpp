// Ported script bodies from qtwebkit-compat for the WPE backend (#81).
#include "scripts.h"

namespace wpewebkit_compat {
namespace scripts {

const char kInjectedScriptName[] = "webos-document-creation";
const char kPrefixedEventScriptName[] = "webos-prefixed-events";
const char kFrameCancelScriptName[] = "webos-frame-cancel";
const char kBorderImageScriptName[] = "webos-border-image";
const char kWindowOpenScriptName[] = "webos-window-open";
const char kRemoteRequestScriptName[] = "webos-remote-requests";

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

} // namespace scripts
} // namespace wpewebkit_compat
