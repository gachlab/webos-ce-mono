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

// node 0.4's V8 API, implemented on N-API.
//
// This is deliberately not a general V8 emulation. It covers what webOS's three
// addons actually call, and nothing else -- see the README for the measured
// list. Anything outside it should fail to compile rather than quietly do
// something different.
//
// The shape of the original API is what makes this possible: a Handle<T> is a
// pointer-sized wrapper around a value the engine owns, and so is a napi_value.
// So Handle<T> becomes a wrapper around napi_value, and the static factories
// (String::New and friends) become the matching napi_create_* calls.
//
// The one piece of global state is the current napi_env. V8 0.4's API is
// implicitly single-isolate -- HandleScope takes no arguments, Context::GetCurrent
// takes none -- so there is nowhere to thread an env through. It is set on entry
// to every call that comes from JavaScript and restored on the way out.

#ifndef WEBOS_V8_SHIM_H
#define WEBOS_V8_SHIM_H

#include <node_api.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace v8 {

// The env in force for the call being handled. See the note above.
napi_env CurrentEnv();
void SetCurrentEnv(napi_env env);

// A scope guard for it, so an early return cannot leave the wrong env behind.
class EnvScope {
public:
    explicit EnvScope(napi_env env) : fPrevious(CurrentEnv()) { SetCurrentEnv(env); }
    ~EnvScope() { SetCurrentEnv(fPrevious); }
private:
    napi_env fPrevious;
};

class Value;
class Object;
class String;
class Function;

// Handle<T>, Local<T> and Persistent<T> were three lifetimes over the same
// representation. Only Persistent needs to differ here: it takes a napi_ref so
// the value survives the handle scope, exactly as it did.
template <typename T>
class Handle {
public:
    Handle() : fValue(nullptr) {}
    Handle(napi_value v) : fValue(v) {}                       // NOLINT: implicit, as in V8
    template <typename U> Handle(const Handle<U>& other) : fValue(other.raw()) {}

    bool IsEmpty() const { return fValue == nullptr; }
    void Clear() { fValue = nullptr; }
    napi_value raw() const { return fValue; }
    operator napi_value() const { return fValue; }

    // V8's Handle<T> held a T* and forwarded through it. Here it holds a
    // napi_value, and every T in this header reads it back as its own first
    // member -- see Value::self() -- so the handle's storage IS the object as
    // far as T is concerned. That is what makes HP's `handle->Method()` compile
    // unchanged.
    T* operator->() const { return reinterpret_cast<T*>(const_cast<Handle*>(this)); }
    T* operator*() const { return reinterpret_cast<T*>(const_cast<Handle*>(this)); }

protected:
    napi_value fValue;
};

template <typename T>
class Local : public Handle<T> {
public:
    Local() {}
    Local(napi_value v) : Handle<T>(v) {}                      // NOLINT
    template <typename U> Local(const Handle<U>& other) : Handle<T>(other.raw()) {}
    static Local<T> Cast(napi_value v) { return Local<T>(v); }
    template <typename U> static Local<T> Cast(const Handle<U>& h) { return Local<T>(h.raw()); }
};


template <typename T>
class Persistent : public Handle<T> {
public:
    Persistent() : fRef(nullptr) {}
    Persistent(napi_value v) : Handle<T>(v), fRef(nullptr) {}   // NOLINT

    // V8 0.4 spelled this Persistent<T>::New(handle). It made the value outlive
    // the enclosing HandleScope; a napi_ref does the same job.
    template <typename U>
    static Persistent<T> New(const Handle<U>& handle) {
        Persistent<T> p;
        napi_env env = CurrentEnv();
        napi_create_reference(env, handle.raw(), 1, &p.fRef);
        p.fValue = handle.raw();
        return p;
    }

    void Dispose() {
        if (fRef) {
            napi_delete_reference(CurrentEnv(), fRef);
            fRef = nullptr;
        }
        this->fValue = nullptr;
    }

    // A napi_value is only valid inside the scope that produced it, so a
    // Persistent has to go back through its reference to be used later.
    napi_value Resolved() const {
        if (!fRef)
            return this->fValue;
        napi_value out = nullptr;
        napi_get_reference_value(CurrentEnv(), fRef, &out);
        return out;
    }

private:
    napi_ref fRef;
};

class HandleScope {
public:
    HandleScope() : fScope(nullptr) {
        napi_open_handle_scope(CurrentEnv(), &fScope);
    }
    ~HandleScope() {
        if (fScope)
            napi_close_handle_scope(CurrentEnv(), fScope);
    }
    // V8 0.4's Close(handle) escaped one value into the enclosing scope. Nothing
    // in webOS's addons uses it, so it is deliberately absent.
private:
    napi_handle_scope fScope;
    HandleScope(const HandleScope&);
    HandleScope& operator=(const HandleScope&);
};

// ---------------------------------------------------------------- values ----

class Value {
public:
    // These are reached through Handle<Value>::operator->, so `this` is the
    // Handle and its first member is the napi_value.
    napi_value self() const { return *reinterpret_cast<napi_value const*>(this); }

    bool IsUndefined() const { return TypeIs(napi_undefined); }
    bool IsNull() const { return TypeIs(napi_null); }
    bool IsString() const { return TypeIs(napi_string); }
    bool IsFunction() const { return TypeIs(napi_function); }
    bool IsNumber() const { return TypeIs(napi_number); }
    bool IsObject() const { return TypeIs(napi_object); }
    bool IsBoolean() const { return TypeIs(napi_boolean); }

    bool IsArray() const {
        bool is = false;
        napi_is_array(CurrentEnv(), self(), &is);
        return is;
    }

    int64_t IntegerValue() const {
        int64_t out = 0;
        napi_value number = nullptr;
        if (napi_coerce_to_number(CurrentEnv(), self(), &number) == napi_ok)
            napi_get_value_int64(CurrentEnv(), number, &out);
        return out;
    }
    int32_t Int32Value() const { return static_cast<int32_t>(IntegerValue()); }
    uint32_t Uint32Value() const { return static_cast<uint32_t>(IntegerValue()); }

    double NumberValue() const {
        double out = 0;
        napi_value number = nullptr;
        if (napi_coerce_to_number(CurrentEnv(), self(), &number) == napi_ok)
            napi_get_value_double(CurrentEnv(), number, &out);
        return out;
    }

    bool BooleanValue() const {
        bool out = false;
        napi_value b = nullptr;
        if (napi_coerce_to_bool(CurrentEnv(), self(), &b) == napi_ok)
            napi_get_value_bool(CurrentEnv(), b, &out);
        return out;
    }

    Local<String> ToString() const {
        napi_value out = nullptr;
        napi_coerce_to_string(CurrentEnv(), self(), &out);
        return Local<String>(out);
    }

    Local<Object> ToObject() const {
        napi_value out = nullptr;
        napi_coerce_to_object(CurrentEnv(), self(), &out);
        return Local<Object>(out);
    }

private:
    bool TypeIs(napi_valuetype want) const {
        napi_valuetype t;
        if (napi_typeof(CurrentEnv(), self(), &t) != napi_ok)
            return false;
        return t == want;
    }
};

class Primitive : public Value {};

class String : public Value {
public:
    static Local<String> New(const char* text, int length = -1) {
        napi_value out = nullptr;
        size_t len = (length < 0) ? NAPI_AUTO_LENGTH : static_cast<size_t>(length);
        napi_create_string_utf8(CurrentEnv(), text ? text : "", len, &out);
        return Local<String>(out);
    }

    // V8 interned these. N-API has no separate symbol table for strings, and
    // nothing in the addons depends on identity, so a plain string will do.
    static Local<String> NewSymbol(const char* text) { return New(text); }

    int Length() const {
        size_t len = 0;
        napi_get_value_string_utf8(CurrentEnv(), self(), nullptr, 0, &len);
        return static_cast<int>(len);
    }

    // String::Utf8Value(handle) -> *value is a NUL-terminated char*.
    class Utf8Value {
    public:
        explicit Utf8Value(const Handle<Value>& value) { Init(value.raw()); }
        explicit Utf8Value(napi_value value) { Init(value); }

        char* operator*() { return fText.empty() ? const_cast<char*>("") : &fText[0]; }
        const char* operator*() const { return fText.empty() ? "" : &fText[0]; }
        int length() const { return fText.empty() ? 0 : static_cast<int>(fText.size() - 1); }

    private:
        void Init(napi_value value) {
            if (!value)
                return;
            napi_env env = CurrentEnv();
            napi_value text = nullptr;
            if (napi_coerce_to_string(env, value, &text) != napi_ok)
                return;
            size_t len = 0;
            if (napi_get_value_string_utf8(env, text, nullptr, 0, &len) != napi_ok)
                return;
            fText.resize(len + 1);
            napi_get_value_string_utf8(env, text, &fText[0], len + 1, &len);
        }
        std::vector<char> fText;
        Utf8Value(const Utf8Value&);
        Utf8Value& operator=(const Utf8Value&);
    };

    // dynaload hands V8 a string it owns and expects it not to be copied. N-API
    // has no external strings, so this copies. The only cost is memory, and the
    // strings involved are loaded JavaScript files.
    class ExternalAsciiStringResource {
    public:
        virtual ~ExternalAsciiStringResource() {}
        virtual const char* data() const = 0;
        virtual size_t length() const = 0;
    };

    static Local<String> NewExternal(ExternalAsciiStringResource* resource) {
        if (!resource)
            return Local<String>();
        Local<String> out = New(resource->data(), static_cast<int>(resource->length()));
        delete resource;
        return out;
    }
};

class Integer : public Primitive {
public:
    static Local<Integer> New(int32_t value) {
        napi_value out = nullptr;
        napi_create_int32(CurrentEnv(), value, &out);
        return Local<Integer>(out);
    }
    static Local<Integer> NewFromUnsigned(uint32_t value) {
        napi_value out = nullptr;
        napi_create_uint32(CurrentEnv(), value, &out);
        return Local<Integer>(out);
    }
};

class Number : public Primitive {
public:
    static Local<Number> New(double value) {
        napi_value out = nullptr;
        napi_create_double(CurrentEnv(), value, &out);
        return Local<Number>(out);
    }
};

class Boolean : public Primitive {
public:
    static Local<Boolean> New(bool value) {
        napi_value out = nullptr;
        napi_get_boolean(CurrentEnv(), value, &out);
        return Local<Boolean>(out);
    }
};

inline Local<Primitive> Undefined() {
    napi_value out = nullptr;
    napi_get_undefined(CurrentEnv(), &out);
    return Local<Primitive>(out);
}

inline Local<Primitive> Null() {
    napi_value out = nullptr;
    napi_get_null(CurrentEnv(), &out);
    return Local<Primitive>(out);
}

inline Local<Boolean> True() { return Boolean::New(true); }
inline Local<Boolean> False() { return Boolean::New(false); }

class Object : public Value {
public:
    static Local<Object> New() {
        napi_value out = nullptr;
        napi_create_object(CurrentEnv(), &out);
        return Local<Object>(out);
    }

    bool Set(const Handle<Value>& key, const Handle<Value>& value) {
        return napi_set_property(CurrentEnv(), self(), key.raw(), value.raw()) == napi_ok;
    }
    bool Set(const char* key, const Handle<Value>& value) {
        return napi_set_named_property(CurrentEnv(), self(), key, value.raw()) == napi_ok;
    }

    Local<Value> Get(const Handle<Value>& key) const {
        napi_value out = nullptr;
        napi_get_property(CurrentEnv(), self(), key.raw(), &out);
        return Local<Value>(out);
    }
    Local<Value> Get(const char* key) const {
        napi_value out = nullptr;
        napi_get_named_property(CurrentEnv(), self(), key, &out);
        return Local<Value>(out);
    }

    bool Has(const Handle<Value>& key) const {
        bool has = false;
        napi_has_property(CurrentEnv(), self(), key.raw(), &has);
        return has;
    }

    // Internal fields were V8's way of hanging a C++ pointer off an object.
    // node::ObjectWrap is the only user here, and it goes through napi_wrap.
    void SetPointerInInternalField(int index, void* value);
    void* GetPointerFromInternalField(int index) const;
    int InternalFieldCount() const { return 1; }
};

class Array : public Object {
public:
    static Local<Array> New(int length = 0) {
        napi_value out = nullptr;
        napi_create_array_with_length(CurrentEnv(), static_cast<size_t>(length), &out);
        return Local<Array>(out);
    }
    uint32_t Length() const {
        uint32_t len = 0;
        napi_get_array_length(CurrentEnv(), self(), &len);
        return len;
    }
    bool Set(uint32_t index, const Handle<Value>& value) {
        return napi_set_element(CurrentEnv(), self(), index, value.raw()) == napi_ok;
    }
    Local<Value> Get(uint32_t index) const {
        napi_value out = nullptr;
        napi_get_element(CurrentEnv(), self(), index, &out);
        return Local<Value>(out);
    }
};

class Function : public Object {
public:
    Local<Value> Call(const Handle<Object>& recv, int argc, Handle<Value> argv[]) {
        std::vector<napi_value> args;
        args.reserve(argc);
        for (int i = 0; i < argc; ++i)
            args.push_back(argv[i].raw());
        napi_value out = nullptr;
        napi_call_function(CurrentEnv(), recv.raw(), self(), static_cast<size_t>(argc),
                           args.empty() ? nullptr : &args[0], &out);
        return Local<Value>(out);
    }

    Local<Object> NewInstance(int argc = 0, Handle<Value> argv[] = nullptr) {
        std::vector<napi_value> args;
        for (int i = 0; i < argc; ++i)
            args.push_back(argv[i].raw());
        napi_value out = nullptr;
        napi_new_instance(CurrentEnv(), self(), static_cast<size_t>(argc),
                          args.empty() ? nullptr : &args[0], &out);
        return Local<Object>(out);
    }

    void SetName(const Handle<String>&) {}   // cosmetic in V8; nothing reads it back
};

// ------------------------------------------------------------- callbacks ----

class Arguments;
typedef Handle<Value> (*InvocationCallback)(const Arguments& args);

class Arguments {
public:
    Arguments(napi_env env, napi_callback_info info);

    int Length() const { return static_cast<int>(fArgs.size()); }
    Local<Value> operator[](int index) const {
        if (index < 0 || index >= Length())
            return Local<Value>(Undefined().raw());
        return Local<Value>(fArgs[index]);
    }
    Local<Object> This() const { return Local<Object>(fThis); }
    Local<Object> Holder() const { return Local<Object>(fThis); }
    Local<Value> Data() const { return Local<Value>(fData); }
    bool IsConstructCall() const { return fIsConstructCall; }

private:
    std::vector<napi_value> fArgs;
    napi_value fThis;
    napi_value fData;
    bool fIsConstructCall;
};

// ------------------------------------------------------------- templates ----

// V8 built classes out of a FunctionTemplate plus its prototype and instance
// templates. N-API builds them with napi_define_class. The two do not line up
// exactly, so this keeps the pieces and defers the class until GetFunction().
class ObjectTemplate;

class FunctionTemplate {
public:
    static Handle<FunctionTemplate> New(InvocationCallback callback = nullptr,
                                        Handle<Value> data = Handle<Value>());

    Local<Function> GetFunction();
    Handle<ObjectTemplate> InstanceTemplate();
    Handle<ObjectTemplate> PrototypeTemplate();
    void SetClassName(const Handle<String>& name);
    void Inherit(const Handle<FunctionTemplate>& parent);

    // Used by NODE_SET_PROTOTYPE_METHOD.
    void SetPrototypeMethod(const char* name, InvocationCallback callback);
};

class ObjectTemplate {
public:
    static Handle<ObjectTemplate> New();
    void SetInternalFieldCount(int) {}                  // always one here
    void Set(const Handle<String>& name, const Handle<Value>& value);
    void SetMethod(const char* name, InvocationCallback callback);
    Local<Object> NewInstance();
};

// ------------------------------------------------------------ exceptions ----

class Exception {
public:
    static Local<Value> Error(const Handle<String>& message);
    static Local<Value> TypeError(const Handle<String>& message);
    static Local<Value> RangeError(const Handle<String>& message);
};

// V8 returned the thrown value and unwound when the callback returned. N-API
// throws immediately and the callback returns undefined, which is what the
// wrapper around InvocationCallback does with the result.
Handle<Value> ThrowException(const Handle<Value>& exception);

class TryCatch {
public:
    TryCatch();
    ~TryCatch();
    bool HasCaught() const;
    Local<Value> Exception() const;
    Local<Value> StackTrace() const;
    void Reset();
private:
    mutable napi_value fError;
};

// --------------------------------------------------------------- context ----

class Context {
public:
    static Handle<Context> GetCurrent();
    static Handle<Context> New() { return GetCurrent(); }
    Local<Object> Global();

    class Scope {
    public:
        explicit Scope(const Handle<Context>&) {}       // one context here
    };
};

class Script {
public:
    static Local<Script> New(const Handle<String>& source,
                             const Handle<String>& name = Handle<String>());
    static Local<Script> Compile(const Handle<String>& source,
                                 const Handle<String>& name = Handle<String>()) {
        return New(source, name);
    }
    Local<Value> Run();
};

class V8 {
public:
    static void SetFlagsFromString(const char*, int) {}
};

}  // namespace v8

#endif /* WEBOS_V8_SHIM_H */
