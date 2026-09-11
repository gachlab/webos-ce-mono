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
    // HP's set the process title. process.title does the same thing.
    process.setName = function (name) {
        try {
            process.title = String(name);
        } catch (e) {
            // Some platforms refuse a longer title than the original argv.
            // Nothing depends on it having worked.
        }
    };
}

if (typeof process.setArgs !== 'function') {
    // HP's rewrote the argv area so a service showed its own arguments in ps.
    // There is no way to do that from JavaScript now, and nothing reads it back.
    process.setArgs = function () {};
}
