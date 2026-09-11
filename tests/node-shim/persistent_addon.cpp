/* Copyright (c) 2026 webOS CE modern build
 * Licensed under the Apache License, Version 2.0. */

// A Persistent must still hold its value after the scope that made it is gone.
//
// This is written the way HP's addons are, against node 0.4's API, so it goes
// through the shim like everything else. It mirrors NODE_PSYMBOL: a
// Persistent<String> filled in at init and read back from a function called
// later -- which is when a bus reply arrives and node_ls2 emits its symbol.
//
// Getting this wrong is silent. napi_create_reference only takes objects: given
// a string it reports success and hands back a null reference, the value expires
// with the init scope, and by the time it is read the slot holds something else.
// The symptom was emit() being called with an object for an event name.

#include <node.h>
#include <v8.h>

using namespace v8;

static Persistent<String> gSymbol;
static Persistent<Object> gObject;

static Handle<Value> ReadSymbol(const Arguments&)
{
    HandleScope scope;
    return gSymbol;
}

static Handle<Value> ReadObjectKey(const Arguments&)
{
    HandleScope scope;
    Handle<Object> o = gObject;
    return o->Get("kept");
}

extern "C" void init(Handle<Object> target)
{
    HandleScope scope;

    gSymbol = NODE_PSYMBOL("response");

    Local<Object> o = Object::New();
    o->Set("kept", String::New("still here"));
    gObject = Persistent<Object>::New(o);

    target->Set(String::NewSymbol("readSymbol"),
                FunctionTemplate::New(ReadSymbol)->GetFunction());
    target->Set(String::NewSymbol("readObjectKey"),
                FunctionTemplate::New(ReadObjectKey)->GetFunction());
}
