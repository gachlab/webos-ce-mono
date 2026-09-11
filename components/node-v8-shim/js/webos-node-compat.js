// The pieces of HP's patched node that a modern one does not have.
//
// webOS shipped its own node build with extra methods on `process`, and
// bootstrap-node.js calls them before anything else runs. They are cosmetic --
// both only affect how the process shows up in ps -- so the compatible thing is
// to provide them rather than to edit HP's launcher.
//
// Loaded with NODE_OPTIONS=--require, so it applies to every node the service
// launcher starts without run-js-service knowing about it.

'use strict';

if (typeof process.setName !== 'function') {
    // HP's set the process title, and process.title does the same thing -- but
    // on Linux that rewrites the process's own argv area, which is what ps and
    // /proc/<pid>/cmdline read.
    //
    // So it has to be a string. bootstrap-node.js calls this with the service
    // description object, and an earlier version coerced it with String(),
    // which set every service's command line to the literal "[object Object]".
    // Nothing could find its own processes after that.
    process.setName = function (name) {
        if (typeof name !== 'string' || name === '') {
            return;
        }
        try {
            process.title = name;
        } catch (e) {
            // Some platforms refuse a title longer than the original argv.
            // Nothing depends on it having worked.
        }
    };
}

if (typeof process.setArgs !== 'function') {
    // HP's rewrote the argv area so a service showed its own arguments in ps.
    // There is no way to do that from JavaScript now, and nothing reads it back.
    process.setArgs = function () {};
}

// `sys` was renamed to `util` in node 0.8 and removed later. mojoloader.js and
// fork_server.js still require it -- two files in the whole runtime path, which
// is why this is an alias rather than an edit to either.
const Module = require('module');
const originalLoad = Module._load;
Module._load = function (request, parent, isMain) {
    if (request === 'sys') {
        return originalLoad.call(this, 'util', parent, isMain);
    }
    return originalLoad.call(this, request, parent, isMain);
};

// new Buffer() throws in a modern node. Two call sites in the frameworks.
if (typeof Buffer !== 'undefined' && !Buffer.__webosCompatPatched) {
    const RealBuffer = Buffer;
    const Patched = function (arg, encodingOrOffset, length) {
        if (!(this instanceof Patched)) {
            return Patched(arg, encodingOrOffset, length);
        }
        if (typeof arg === 'number') {
            return RealBuffer.alloc(arg);
        }
        return RealBuffer.from(arg, encodingOrOffset, length);
    };
    Patched.prototype = RealBuffer.prototype;
    Object.setPrototypeOf(Patched, RealBuffer);
    Patched.__webosCompatPatched = true;
    global.Buffer = Patched;
}

// Globals HP's node build provided and a stock one does not.
//
// mojoloader copies these from the loading environment into each library it
// loads (see _propogateGlobals), so defining them here is enough for the
// frameworks and services to find them.
const fs = require('fs');

if (typeof global.palmGetResource !== 'function') {
    // Reads a file and returns its contents. mojoloader uses it to load
    // JavaScript, and mojoservice's AppController to read services.json.
    global.palmGetResource = function (path) {
        try {
            return fs.readFileSync(path, 'utf8');
        } catch (e) {
            return undefined;
        }
    };
}

if (typeof global.palmPutResource !== 'function') {
    global.palmPutResource = function (path, contents) {
        try {
            fs.writeFileSync(path, contents);
            return true;
        } catch (e) {
            return false;
        }
    };
}

if (typeof global.quit !== 'function') {
    global.quit = function (code) {
        process.exit(typeof code === 'number' ? code : 0);
    };
}

if (typeof global.getenv !== 'function') {
    global.getenv = function (name) {
        return process.env[name];
    };
}
