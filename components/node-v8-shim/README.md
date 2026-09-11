# node-v8-shim

webOS's three node addons are written against node 0.4's V8 API, from 2011:
`Handle<Value>`, `Arguments`, `String::NewSymbol`, `ThrowException`, a
`HandleScope` with no isolate, `extern "C" void init(Handle<Object>)`. None of it
exists in a modern node.

Rather than rewrite roughly 3100 lines of HP's code, this provides the interface
it already calls, implemented on N-API. The addons compile unchanged.

N-API is the right floor to build on: it is ABI-stable, so what is built here
keeps loading on later node releases without recompiling, which is the whole
reason this problem exists in the first place.

## What it covers

Measured across the three addons, the V8 surface they touch is about thirty
names, and very repetitive:

    Handle / Local / Persistent      HandleScope
    String::New / NewSymbol          String::Utf8Value
    ThrowException / Exception       Arguments
    Object / Array / Function        FunctionTemplate / ObjectTemplate
    Integer::New / Boolean::New      Undefined
    Script / Context                 TryCatch

plus, from node itself, `node::ObjectWrap` (32 uses) and
`NODE_SET_PROTOTYPE_METHOD` (22).

## What it does not cover, and why

`nodejs-module-webos-sysbus/src/node_ls2.cpp` pumps a GLib main context from
inside node's event loop. In node 0.4 that loop was libev, and the file is built
out of `ev_prepare`, `ev_check` and `ev_io`. Modern node uses libuv, so the same
idea has to be expressed with `uv_prepare_t`, `uv_check_t` and `uv_poll_t`.

That is a real rewrite, not a shim -- but it is one file and one well-understood
pattern, and everything else stays as HP wrote it.
