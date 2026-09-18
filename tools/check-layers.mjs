// The rules that make the web SDK three layers instead of three directories.
//
//     tools/check-layers.mjs        (tools/test-web.sh runs it)
//
// npm's workspaces put every package in node_modules, so node resolves any of
// these happily and tsconfig.web.json type-checks all four packages as one
// program. Nothing in the toolchain says no. This does.
//
// It reads import specifiers rather than grepping for names, because the first
// version of these checks was three `grep -rl` lines and two of them could not
// fire:
//
//   * `--include='luna/*.ts'` matches nothing at all -- GNU grep matches an
//     --include glob against the BASE NAME, so a glob with a slash in it never
//     matches a file. That guard had never run.
//   * a relative escape was matched as `"../../../`, which misses the
//     two-level ones: from sdk/ui-kit/src, `"../../webos-api/src/..."` walks
//     straight into the other package and type-checks.
//
// Verified by mutation: each rule below turns red when the import it forbids is
// added to a file it covers, including into apps/wifi/src/luna/.

import { readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";

const root = resolve(import.meta.dirname, "..");

// Every specifier, in every form it can be written: `from "x"`, a bare
// `import "x"`, and `import("x")`, single- or double-quoted.
const SPECIFIER = /(?:\bfrom|\bimport)\s*\(?\s*(["'])([^"']+)\1/g;

const walk = (dir) => {
    const found = [];
    for (const name of readdirSync(dir)) {
        if (name === "node_modules") continue;
        const path = join(dir, name);
        if (statSync(path).isDirectory()) found.push(...walk(path));
        else if (name.endsWith(".ts")) found.push(path);
    }
    return found;
};

const packages = [];
for (const group of ["sdk", "apps"]) {
    for (const name of readdirSync(join(root, group))) {
        const path = join(root, group, name);
        if (statSync(path).isDirectory() && readdirSync(path).includes("package.json")) {
            packages.push({ name: `${group}/${name}`, path });
        }
    }
}

const problems = [];
const fail = (file, why) => problems.push(`${relative(root, file)}: ${why}`);

for (const pkg of packages) {
    for (const file of walk(pkg.path)) {
        const source = readFileSync(file, "utf8");
        for (const [, , specifier] of source.matchAll(SPECIFIER)) {
            // 1. A relative import must stay inside its own package. This is
            //    the one that makes "the layering is resolution, not
            //    discipline" true rather than aspirational.
            if (specifier.startsWith(".")) {
                const target = resolve(dirname(file), specifier);
                if (!target.startsWith(pkg.path + "/")) {
                    fail(file, `"${specifier}" leaves ${pkg.name}. Cross a package by its name.`);
                }
                continue;
            }

            // 2. @webos/api is the platform: the bus, the card's own life,
            //    translation. It must not know there is a screen, or a card
            //    could not be written for this device in anything but our kit.
            if (pkg.name === "sdk/webos-api"
                && (specifier.startsWith("@webos/ui-kit") || specifier.startsWith("@gachlab/"))) {
                fail(file, `the platform package must not import "${specifier}".`);
            }

            // 3. In an app, only the file that draws may name the kit. An
            //    app's logic is the layer a card written in something else
            //    would reuse -- it used to live in src/lib, where the old rule
            //    covered it, and it moved into the apps when the SDK was split.
            if (pkg.name.startsWith("apps/")
                && specifier.startsWith("@webos/ui-kit")
                && !file.endsWith("/src/main.ts")) {
                fail(file, `an app's logic must not import "${specifier}"; keep it in main.ts.`);
            }

            // 4. And the example stays an example. apps/example-plain exists to
            //    prove a card can be written without our renderer (#65), so the
            //    day someone reaches for startCard "to save a few lines", the
            //    proof quietly stops proving anything. Its ELEMENTS are what it
            //    is meant to import; the machinery that draws them is not, and
            //    neither is lit-html, whose absence is the README's first claim.
            if (pkg.name === "apps/example-plain"
                && (/^@webos\/ui-kit\/(start-card|element)/.test(specifier)
                    || specifier === "lit-html" || specifier.startsWith("lit-html/"))) {
                fail(file, `the card that proves the runtime is optional must not import "${specifier}".`);
            }
        }
    }
}

if (problems.length > 0) {
    console.log("FAIL: the layers do not hold");
    for (const problem of problems) console.log(`  ${problem}`);
    process.exit(1);
}
console.log(`the layers hold across ${packages.length} packages                       ok`);
