/* @@@LICENSE
*
*      Copyright (c) 2010-2012 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

#include <node.h>
#include <iostream>
#include <syslog.h>
#include <v8.h>

#if HAS_PMLOGLIB
#include <PmLogLib.h>
#endif

#include "pmloglib.js.h"

using namespace v8;
using namespace std;

#if !HAS_PMLOGLIB
void LogString(int level, const char* label, const char* stringToLog)
{
	static bool sysLogOpened = false;
	if (!sysLogOpened) {
		openlog(label, 0, 0);
		sysLogOpened = true;
	}
	syslog(level, "%s", stringToLog);
	if (level == LOG_ERR) {
        cerr << stringToLog << endl;	    
	} else {
        cout << stringToLog << endl;	    
	}
}
#else
static void LogString(int level, const char* label, const char* stringToLog)
{
        PmLogContext jsContext;
        PmLogGetContext(label, &jsContext);

	switch(level) {
	default:
		PmLogPrintInfo(jsContext, "%s", stringToLog);
		break;
	case LOG_WARNING:
		PmLogPrintWarning(jsContext, "%s", stringToLog);
		break;
	case LOG_ERR:
        PmLogPrintError(jsContext, "%s", stringToLog);
		break;
	}
        cerr << stringToLog << endl;
}
#endif

static Handle<Value> LogWrapper(const Arguments& args)
{   
    if (args.Length() != 3) {
        return ThrowException(v8::Exception::Error(
                                  v8::String::New("Invalid number of parameters, 3 expected.")));
    }
    
    int logLevel = args[0]->IntegerValue();
	String::Utf8Value label(args[1]);
	String::Utf8Value stringToLog(args[2]);
    LogString(logLevel, *label, *stringToLog);
    return args[2];
}

extern "C" void
init(Handle<Object> target)
{
    HandleScope scope;
    Local<FunctionTemplate> logFunction = FunctionTemplate::New(LogWrapper);
    target->Set(String::NewSymbol("_logString"), logFunction->GetFunction());
    target->Set(String::NewSymbol("LOG_ERR"), Integer::New(LOG_ERR));
    target->Set(String::NewSymbol("LOG_WARNING"), Integer::New(LOG_WARNING));
    target->Set(String::NewSymbol("LOG_INFO"), Integer::New(LOG_INFO));
    Local<String> scriptText = String::New((const char*)pmloglib_js, pmloglib_js_len);
    Local<Script> script = Script::New(scriptText, String::New("pmloglib.js"));
    if (!script.IsEmpty()) {
        Local<Value> v = script->Run();
        Local<Function> f = Local<Function>::Cast(v);
    	Local<Context> current = Context::GetCurrent();
        Handle<Value> argv[1];
        argv[0] = target;
        f->Call(current->Global(), 1, &argv[0]);
    } else {
        cerr << "Script was empty." << endl;
    }
}
