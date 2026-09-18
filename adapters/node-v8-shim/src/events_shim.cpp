/* Copyright (c) 2026 webOS CE modern build
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "node_events.h"

#include <string>


namespace {

// Only what webOS calls. method_dispatcher.js uses addListener('request') and
// addListener('cancel'); node_ls2 emits 'request', 'response' and 'cancel'.
//
// Node's own EventEmitter would be better, but reaching it means calling
// require() from inside the addon and N-API has no way to do that.
const char* kEmitterJs = R"JS(
(function (proto) {
    function listeners(self, name) {
        if (!self.__events) { self.__events = {}; }
        if (!self.__events[name]) { self.__events[name] = []; }
        return self.__events[name];
    }
    proto.addListener = function (name, fn) {
        listeners(this, name).push(fn);
        return this;
    };
    proto.on = proto.addListener;
    proto.once = function (name, fn) {
        var self = this;
        function wrapper() {
            self.removeListener(name, wrapper);
            return fn.apply(self, arguments);
        }
        return this.addListener(name, wrapper);
    };
    proto.removeListener = function (name, fn) {
        var list = listeners(this, name);
        var i = list.indexOf(fn);
        if (i >= 0) { list.splice(i, 1); }
        return this;
    };
    proto.removeAllListeners = function (name) {
        if (name === undefined) { this.__events = {}; }
        else { listeners(this, name).length = 0; }
        return this;
    };
    proto.listeners = function (name) {
        return listeners(this, name).slice();
    };
    proto.emit = function (name) {
        var list = listeners(this, name);
        if (list.length === 0) { return false; }
        var args = Array.prototype.slice.call(arguments, 1);
        // Copied first: a listener may remove itself while being called.
        var copy = list.slice();
        for (var i = 0; i < copy.length; ++i) { copy[i].apply(this, args); }
        return true;
    };
})
)JS";

}  // namespace

namespace v8 {
// Defined in v8_shim.cpp; applies the emitter to a class prototype once the
// class exists.
void ApplyEventEmitterTo(napi_value prototype);
}  // namespace v8

namespace v8 {

void ApplyEventEmitterTo(napi_value prototype)
{
    napi_env env = CurrentEnv();
    if (!env || !prototype)
        return;

    napi_value source = nullptr;
    if (napi_create_string_utf8(env, kEmitterJs, NAPI_AUTO_LENGTH, &source) != napi_ok)
        return;

    napi_value installer = nullptr;
    if (napi_run_script(env, source, &installer) != napi_ok) {
        napi_value pending = nullptr;
        napi_get_and_clear_last_exception(env, &pending);
        return;
    }

    napi_value global = nullptr;
    napi_get_global(env, &global);
    napi_value result = nullptr;
    if (napi_call_function(env, global, installer, 1, &prototype, &result) != napi_ok) {
        napi_value pending = nullptr;
        napi_get_and_clear_last_exception(env, &pending);
    }
}

}  // namespace v8

namespace node {

// A template handle that Inherit() can be given. It carries no behaviour; the
// FunctionTemplate machinery only checks whether this is what it was passed.
v8::Handle<v8::FunctionTemplate> EventEmitter::constructor_template;

bool EventEmitter::Emit(v8::Handle<v8::String> symbol, int argc, v8::Handle<v8::Value>* argv)
{
    napi_env env = v8::CurrentEnv();
    v8::Local<v8::Object> self = handle();
    if (!env || self.IsEmpty())
        return false;

    napi_value emit = nullptr;
    if (napi_get_named_property(env, self.raw(), "emit", &emit) != napi_ok)
        return false;

    napi_valuetype type;
    if (napi_typeof(env, emit, &type) != napi_ok || type != napi_function)
        return false;

    std::vector<napi_value> args;
    args.reserve(argc + 1);
    args.push_back(symbol.raw());
    for (int i = 0; i < argc; ++i)
        args.push_back(argv[i].raw());

    napi_value result = nullptr;
    if (napi_call_function(env, self.raw(), emit, args.size(), &args[0], &result) != napi_ok) {
        // A listener threw. node 0.4's Emit swallowed it too; there is no
        // JavaScript frame below this to carry it up to.
        napi_value pending = nullptr;
        napi_get_and_clear_last_exception(env, &pending);
        return false;
    }

    bool handled = false;
    napi_get_value_bool(env, result, &handled);
    return handled;
}

}  // namespace node
