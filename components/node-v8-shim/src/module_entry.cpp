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

// node 0.4 loaded an addon by dlopening it and calling a symbol named "init".
// A modern node looks for a NAPI module registration instead. Every webOS addon
// still defines init, so this file is linked into each of them and is the one
// place that knows about the difference.

#include "v8.h"

extern "C" void init(v8::Handle<v8::Object> target);

static napi_value ShimInit(napi_env env, napi_value exports)
{
    v8::SetModuleEnv(env);
    v8::EnvScope scope(env);
    init(v8::Handle<v8::Object>(exports));
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, ShimInit)
