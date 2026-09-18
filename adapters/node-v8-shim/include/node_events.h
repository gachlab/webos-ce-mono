/* Copyright (c) 2026 webOS CE modern build
 * Licensed under the Apache License, Version 2.0. See node.h for the notice. */

// node 0.4's C++ EventEmitter.
//
// It is gone from modern node: events are a JavaScript concern now, and there is
// no C++ class to inherit from. webOS needs it because LS2Base derives from it
// and calls Emit() when a message arrives from the bus, and because the JavaScript
// side does `handle.addListener('request', ...)`.
//
// So the shim supplies both halves. Emit() calls the object's own emit(), and a
// small EventEmitter written in JavaScript is spliced onto the prototype of any
// class whose template says it Inherits from constructor_template -- which is
// what node_ls2_handle.cpp and node_ls2_call.cpp already ask for.
//
// It is deliberately not node's EventEmitter. Reaching node's would mean calling
// require() from inside the addon, which N-API does not offer. What webOS uses --
// addListener, on, once, removeListener, removeAllListeners, listeners, emit --
// is implemented here and nothing else.

#ifndef WEBOS_NODE_EVENTS_SHIM_H
#define WEBOS_NODE_EVENTS_SHIM_H

#include "node.h"

namespace node {

class EventEmitter : public ObjectWrap {
public:
    // Marks a FunctionTemplate as wanting the emitter methods on its prototype.
    static v8::Handle<v8::FunctionTemplate> constructor_template;

protected:
    // Calls this object's emit(symbol, ...argv) and reports whether anything
    // was listening, which is what node 0.4 returned.
    bool Emit(v8::Handle<v8::String> symbol, int argc, v8::Handle<v8::Value>* argv);
};

}  // namespace node

#endif /* WEBOS_NODE_EVENTS_SHIM_H */
