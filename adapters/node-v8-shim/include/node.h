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

// The parts of node 0.4's addon API that webOS's modules use, on N-API.
// Measured, not guessed: ObjectWrap::Unwrap (33 calls), ObjectWrap::Wrap (3),
// NODE_SET_PROTOTYPE_METHOD (22), and the extern "C" init entry point.
//
// Ref, Unref, MakeWeak and handle_ are not here because nothing calls them. A
// future caller should fail to compile rather than get a stub.

#ifndef WEBOS_NODE_SHIM_H
#define WEBOS_NODE_SHIM_H

#include "v8.h"

namespace node {

class ObjectWrap {
public:
    ObjectWrap() {}
    virtual ~ObjectWrap() {}

    // V8 stored the C++ pointer in an internal field. N-API stores it with
    // napi_wrap, which also ties its lifetime to the JavaScript object.
    template <class T>
    static T* Unwrap(v8::Handle<v8::Object> handle) {
        return static_cast<T*>(UnwrapInternal(handle));
    }

    v8::Local<v8::Object> handle() const;

    // node 0.4 used these to keep the JavaScript object alive while C++ still
    // held the wrapper -- LS2Call refs itself for the lifetime of a bus call.
    // A napi_ref counts the same way.
    //
    // (These were missed when the shim's surface was first measured: the calls
    //  are unqualified, so grepping for ObjectWrap::Ref found nothing.)
    virtual void Ref();
    virtual void Unref();

protected:
    void Wrap(v8::Handle<v8::Object> handle);

private:
    static void* UnwrapInternal(v8::Handle<v8::Object> handle);
    v8::Persistent<v8::Object> fHandle;
};

void SetPrototypeMethod(v8::Handle<v8::FunctionTemplate> tpl, const char* name,
                        v8::InvocationCallback callback);
void SetMethod(v8::Handle<v8::Object> target, const char* name,
               v8::InvocationCallback callback);

}  // namespace node

#define NODE_SET_PROTOTYPE_METHOD(tpl, name, callback) \
    node::SetPrototypeMethod((tpl), (name), (callback))

#define NODE_SET_METHOD(target, name, callback) \
    node::SetMethod((target), (name), (callback))

// node 0.4 interned the string and made it persistent. Strings are not interned
// separately here and nothing compares these by identity, so a plain string is
// enough -- but it still has to survive the handle scope, hence Persistent.
#define NODE_PSYMBOL(text) \
    v8::Persistent<v8::String>::New(v8::String::NewSymbol(text))

#endif /* WEBOS_NODE_SHIM_H */
