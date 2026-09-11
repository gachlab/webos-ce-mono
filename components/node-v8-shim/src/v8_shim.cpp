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

#include "v8.h"
#include "node.h"

#include <map>

namespace v8 {

// node 0.4's V8 API is implicitly single-isolate: HandleScope takes no
// arguments, Context::GetCurrent takes none, String::New takes none. There is
// nowhere to pass an env, so it is kept here and set on entry to every call that
// arrives from JavaScript. Addons are loaded on one thread and stay there, so
// thread_local is belt and braces rather than a requirement.
static thread_local napi_env gEnv = nullptr;

napi_env CurrentEnv() { return gEnv; }
void SetCurrentEnv(napi_env env) { gEnv = env; }

Arguments::Arguments(napi_env env, napi_callback_info info)
    : fThis(nullptr), fData(nullptr), fIsConstructCall(false)
{
    size_t argc = 0;
    napi_get_cb_info(env, info, &argc, nullptr, &fThis, nullptr);
    fArgs.resize(argc);
    void* data = nullptr;
    napi_get_cb_info(env, info, &argc, argc ? &fArgs[0] : nullptr, &fThis, &data);

    // The data slot carries the InvocationCallback, not anything the addon put
    // there, so Data() reports undefined rather than handing back a raw pointer.
    napi_get_undefined(env, &fData);

    napi_value target = nullptr;
    if (napi_get_new_target(env, info, &target) == napi_ok && target != nullptr)
        fIsConstructCall = true;
}

// Bridges an InvocationCallback into a napi_callback. The callback pointer
// travels in the function's data slot.
static napi_value CallbackBridge(napi_env env, napi_callback_info info)
{
    EnvScope scope(env);

    void* data = nullptr;
    size_t argc = 0;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, &data);
    InvocationCallback callback = reinterpret_cast<InvocationCallback>(data);
    if (!callback)
        return nullptr;

    Arguments args(env, info);
    Handle<Value> result = callback(args);

    // ThrowException has already thrown by this point; returning anything while
    // an exception is pending is an error, so hand back nothing.
    bool pending = false;
    napi_is_exception_pending(env, &pending);
    if (pending)
        return nullptr;

    return result.IsEmpty() ? nullptr : result.raw();
}

static napi_value MakeFunction(const char* name, InvocationCallback callback)
{
    napi_value fn = nullptr;
    napi_create_function(CurrentEnv(), name ? name : "", NAPI_AUTO_LENGTH,
                         CallbackBridge, reinterpret_cast<void*>(callback), &fn);
    return fn;
}

// --- FunctionTemplate -------------------------------------------------------
//
// V8 kept the callback, the class name and the prototype methods in a template
// and built the function on demand. N-API's napi_define_class wants all of it at
// once, so the pieces are collected here, keyed by the template handle, and the
// class is defined when GetFunction() asks for it.

namespace {

struct TemplateState {
    InvocationCallback constructor = nullptr;
    std::string className;
    std::vector<napi_property_descriptor> methods;
    std::vector<std::string> methodNames;   // the descriptors point into these
    napi_ref definedClass = nullptr;
};

// The state hangs off the handle object itself with napi_wrap.
//
// The first version kept it in a std::map keyed by the napi_value. That is
// wrong: a napi_value is a slot in a handle scope, not an identity -- the same
// address is handed out again once the scope closes, so the lookup found the
// wrong state or none. It is why the embedded JavaScript in pmloglib silently
// did not run.
void ReleaseState(napi_env, void* data, void*)
{
    delete static_cast<TemplateState*>(data);
}

TemplateState* StateFor(napi_value key)
{
    void* out = nullptr;
    if (napi_unwrap(CurrentEnv(), key, &out) != napi_ok)
        return nullptr;
    return static_cast<TemplateState*>(out);
}

}  // namespace

Handle<FunctionTemplate> FunctionTemplate::New(InvocationCallback callback, Handle<Value>)
{
    // The handle needs to be something unique that survives long enough to key
    // the state; an empty object does the job and costs nothing.
    napi_value key = nullptr;
    napi_create_object(CurrentEnv(), &key);

    TemplateState* state = new TemplateState();
    state->constructor = callback;
    napi_wrap(CurrentEnv(), key, state, ReleaseState, nullptr, nullptr);

    // The handle has to outlive the scope it was made in, or the template is
    // gone by the time GetFunction() is called.
    napi_ref keep = nullptr;
    napi_create_reference(CurrentEnv(), key, 1, &keep);
    return Handle<FunctionTemplate>(key);
}

void FunctionTemplate::SetClassName(const Handle<String>& name)
{
    TemplateState* state = StateFor(*reinterpret_cast<napi_value const*>(this));
    if (!state)
        return;
    String::Utf8Value text(name.raw());
    state->className = *text;
}

void FunctionTemplate::SetPrototypeMethod(const char* name, InvocationCallback callback)
{
    TemplateState* state = StateFor(*reinterpret_cast<napi_value const*>(this));
    if (!state)
        return;
    state->methodNames.push_back(name);
    napi_property_descriptor d = {};
    d.utf8name = state->methodNames.back().c_str();
    d.method = CallbackBridge;
    d.data = reinterpret_cast<void*>(callback);
    d.attributes = napi_default;
    state->methods.push_back(d);
}

Local<Function> FunctionTemplate::GetFunction()
{
    napi_value key = *reinterpret_cast<napi_value const*>(this);
    TemplateState* state = StateFor(key);
    if (!state)
        return Local<Function>();

    napi_env env = CurrentEnv();
    if (state->definedClass) {
        napi_value out = nullptr;
        napi_get_reference_value(env, state->definedClass, &out);
        return Local<Function>(out);
    }

    // The descriptors' utf8name pointers must stay valid across the call, which
    // is why methodNames is a deque-like vector of strings owned by the state.
    napi_value cls = nullptr;
    const char* name = state->className.empty() ? "" : state->className.c_str();
    napi_status status = napi_define_class(
        env, name, NAPI_AUTO_LENGTH, CallbackBridge,
        reinterpret_cast<void*>(state->constructor),
        state->methods.size(), state->methods.empty() ? nullptr : &state->methods[0],
        &cls);
    if (status != napi_ok)
        return Local<Function>();

    napi_create_reference(env, cls, 1, &state->definedClass);
    return Local<Function>(cls);
}

Handle<ObjectTemplate> FunctionTemplate::InstanceTemplate()
{
    // V8 separated the instance and prototype templates. Everything webOS does
    // with either ends up on the prototype, so both report the same handle and
    // ObjectTemplate::SetMethod forwards to SetPrototypeMethod.
    return Handle<ObjectTemplate>(*reinterpret_cast<napi_value const*>(this));
}

Handle<ObjectTemplate> FunctionTemplate::PrototypeTemplate()
{
    return Handle<ObjectTemplate>(*reinterpret_cast<napi_value const*>(this));
}

void FunctionTemplate::Inherit(const Handle<FunctionTemplate>&)
{
    // node_ls2 never calls it; left as a no-op so a future caller fails loudly
    // in a test rather than silently getting a broken prototype chain.
}

Handle<ObjectTemplate> ObjectTemplate::New()
{
    napi_value key = nullptr;
    napi_create_object(CurrentEnv(), &key);
    napi_wrap(CurrentEnv(), key, new TemplateState(), ReleaseState, nullptr, nullptr);
    napi_ref keep = nullptr;
    napi_create_reference(CurrentEnv(), key, 1, &keep);
    return Handle<ObjectTemplate>(key);
}

void ObjectTemplate::SetMethod(const char* name, InvocationCallback callback)
{
    reinterpret_cast<FunctionTemplate*>(this)->SetPrototypeMethod(name, callback);
}

void ObjectTemplate::Set(const Handle<String>& name, const Handle<Value>& value)
{
    napi_value key = *reinterpret_cast<napi_value const*>(this);
    napi_set_property(CurrentEnv(), key, name.raw(), value.raw());
}

Local<Object> ObjectTemplate::NewInstance()
{
    napi_value out = nullptr;
    napi_create_object(CurrentEnv(), &out);
    return Local<Object>(out);
}

// --- internal fields --------------------------------------------------------

void Object::SetPointerInInternalField(int, void* value)
{
    napi_wrap(CurrentEnv(), self(), value, nullptr, nullptr, nullptr);
}

void* Object::GetPointerFromInternalField(int) const
{
    void* out = nullptr;
    napi_unwrap(CurrentEnv(), self(), &out);
    return out;
}

// --- exceptions -------------------------------------------------------------

static Local<Value> MakeError(const Handle<String>& message,
                              napi_status (*create)(napi_env, napi_value, napi_value, napi_value*))
{
    napi_value out = nullptr;
    create(CurrentEnv(), nullptr, message.raw(), &out);
    return Local<Value>(out);
}

Local<Value> Exception::Error(const Handle<String>& message)
{
    return MakeError(message, napi_create_error);
}
Local<Value> Exception::TypeError(const Handle<String>& message)
{
    return MakeError(message, napi_create_type_error);
}
Local<Value> Exception::RangeError(const Handle<String>& message)
{
    return MakeError(message, napi_create_range_error);
}

Handle<Value> ThrowException(const Handle<Value>& exception)
{
    napi_throw(CurrentEnv(), exception.raw());
    // V8 returned the value and unwound at the end of the callback. Here the
    // throw is already registered, and CallbackBridge notices it and returns
    // nothing, so what comes back only has to be empty.
    return Handle<Value>();
}

TryCatch::TryCatch() : fError(nullptr) {}

TryCatch::~TryCatch() {}

bool TryCatch::HasCaught() const
{
    bool pending = false;
    napi_is_exception_pending(CurrentEnv(), &pending);
    if (pending && !fError)
        napi_get_and_clear_last_exception(CurrentEnv(), &fError);
    return fError != nullptr;
}

Local<Value> TryCatch::Exception() const { return Local<Value>(fError); }

Local<Value> TryCatch::StackTrace() const
{
    if (!fError)
        return Local<Value>();
    napi_value stack = nullptr;
    napi_get_named_property(CurrentEnv(), fError, "stack", &stack);
    return Local<Value>(stack);
}

void TryCatch::Reset() { fError = nullptr; }

// --- context and scripts ----------------------------------------------------

Handle<Context> Context::GetCurrent()
{
    napi_value global = nullptr;
    napi_get_global(CurrentEnv(), &global);
    return Handle<Context>(global);
}

Local<Object> Context::Global()
{
    napi_value global = nullptr;
    napi_get_global(CurrentEnv(), &global);
    return Local<Object>(global);
}

// V8 compiled a script and ran it later. N-API only offers napi_run_script,
// which does both, so the source is held and run on demand.
// A Script is just its source held until Run() asks for it, on the same
// principle as the templates above: attached to the handle, not kept in a map
// keyed by a napi_value.
// The source is kept as text, not as a reference to the JavaScript string.
// napi_create_reference is for objects; handing it a string returns a null ref
// and the script then silently does not run -- which is exactly what happened,
// and why pmloglib loaded with its constants but without info(), warn() and the
// rest that its embedded JavaScript adds.
namespace {
void ReleaseScriptText(napi_env, void* data, void*)
{
    delete static_cast<std::string*>(data);
}
}  // namespace

Local<Script> Script::New(const Handle<String>& source, const Handle<String>&)
{
    napi_env env = CurrentEnv();
    napi_value key = nullptr;
    napi_create_object(env, &key);

    String::Utf8Value text(source.raw());
    napi_wrap(env, key, new std::string(*text, text.length()),
              ReleaseScriptText, nullptr, nullptr);

    napi_ref keep = nullptr;
    napi_create_reference(env, key, 1, &keep);
    return Local<Script>(key);
}

Local<Value> Script::Run()
{
    napi_env env = CurrentEnv();
    napi_value key = *reinterpret_cast<napi_value const*>(this);
    void* wrapped = nullptr;
    if (napi_unwrap(env, key, &wrapped) != napi_ok || !wrapped)
        return Local<Value>();

    const std::string& text = *static_cast<std::string*>(wrapped);
    napi_value source = nullptr;
    if (napi_create_string_utf8(env, text.c_str(), text.size(), &source) != napi_ok)
        return Local<Value>();

    napi_value out = nullptr;
    if (napi_run_script(env, source, &out) != napi_ok) {
        napi_value pending = nullptr;
        napi_get_and_clear_last_exception(env, &pending);
        return Local<Value>();
    }
    return Local<Value>(out);
}

}  // namespace v8

// --- node -------------------------------------------------------------------

namespace node {

void ObjectWrap::Wrap(v8::Handle<v8::Object> handle)
{
    fHandle = v8::Persistent<v8::Object>::New(handle);
    napi_wrap(v8::CurrentEnv(), handle.raw(), this, nullptr, nullptr, nullptr);
}

v8::Local<v8::Object> ObjectWrap::handle() const
{
    return v8::Local<v8::Object>(fHandle.Resolved());
}

void* ObjectWrap::UnwrapInternal(v8::Handle<v8::Object> handle)
{
    void* out = nullptr;
    napi_unwrap(v8::CurrentEnv(), handle.raw(), &out);
    return out;
}

void SetPrototypeMethod(v8::Handle<v8::FunctionTemplate> tpl, const char* name,
                        v8::InvocationCallback callback)
{
    tpl->SetPrototypeMethod(name, callback);
}

void SetMethod(v8::Handle<v8::Object> target, const char* name,
               v8::InvocationCallback callback)
{
    v8::Local<v8::Function> fn = v8::FunctionTemplate::New(callback)->GetFunction();
    target->Set(name, fn);
}

}  // namespace node
