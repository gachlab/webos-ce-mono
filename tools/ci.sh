#!/bin/bash
# Builds and tests the tree in a container, for one Debian release.
#
#   tools/ci.sh trixie      # Debian stable
#   tools/ci.sh sid         # what development happens on
#   tools/ci.sh             # every target in TARGETS, in order
#
# Why this exists: everything about this project has been verified on exactly
# one machine, with one Qt. "It builds from scratch" is a promise to someone
# else's computer, and the cheapest way to keep it honest is to build somewhere
# that is not ours, on every push.
#
# Two things it does deliberately:
#
#   * It builds from `git archive HEAD`, not from the working tree. CI should
#     test what is committed -- an uncommitted file that makes the build pass
#     locally is precisely the failure this is meant to catch -- and it leaves
#     the host's build/ untouched.
#
#   * Dependencies are installed with the network ON, in an image that caches.
#     The build itself then runs with `--network none`, so a component reaching
#     for a download fails instead of quietly succeeding. That is the same check
#     tools/build.sh got under bwrap by hand, made permanent.
#
# Runner: docker or podman, whichever is present. Nothing else is needed on the
# host, and the host's own distro is irrelevant -- which is the point.
set -u

R="$(cd "$(dirname "$0")/.." && pwd)"

# The releases worth knowing about. Debian stable is the one that matters for
# anyone else: sid is where this was developed and proves nothing about
# portability.
TARGETS=(trixie sid)

RUNNER="$(command -v podman || command -v docker || true)"
if [ -z "$RUNNER" ]; then
    echo "ci: neither podman nor docker on PATH" >&2
    exit 2
fi

# Derived from the pkg_check_modules and find_package calls across the tree, not
# from the README -- see docs/component-inventory.md for how the list was taken.
# Qt: base, declarative and webengine, each with its -private- half, because the
# adapters use QMutableEventPoint and QQuickRenderControl.
read -r -d '' PACKAGES <<'PKGS'
build-essential cmake pkg-config
autoconf automake libtool
python3
qt6-base-dev qt6-base-private-dev
qt6-declarative-dev qt6-declarative-private-dev
qt6-webengine-dev qt6-scxml-dev
libglib2.0-dev libglibmm-2.4-dev libsigc++-2.0-dev dbus-daemon
libsqlite3-dev libssl-dev libxml2-dev libyajl-dev libicu-dev
libdb5.3-dev libcurl4-openssl-dev zlib1g-dev
libboost-filesystem-dev libboost-regex-dev libboost-program-options-dev
libc-ares-dev liburiparser-dev
curl xz-utils ca-certificates
PKGS

build_image() {                 # build_image <release>
    local rel="$1" tag="webos-ce-ci:$rel"
    echo "== image for debian:$rel =="
    # Network is on here, and only here.
    # The build context is two files taken from HEAD, not ".". Found in review:
    # "." was whatever directory the script was started from, so run from
    # anywhere but the repository root the COPY below found no
    # tools/node-version and the image failed; and it was the working tree, so
    # an uncommitted pin could reach an image meant to test only what is
    # committed. (It was never the upload it looks like with docker: BuildKit
    # sent 3.19 kB, only what COPY names. Podman would send all of it.)
    local ctx status
    ctx="$(mktemp -d)"
    mkdir -p "$ctx/tools"
    if ! git -C "$R" show HEAD:tools/node-version > "$ctx/tools/node-version" \
       || ! git -C "$R" show HEAD:tools/fetch-node.sh > "$ctx/tools/fetch-node.sh" \
       || ! git -C "$R" show HEAD:package.json > "$ctx/package.json" \
       || ! git -C "$R" show HEAD:package-lock.json > "$ctx/package-lock.json"; then
        echo "  FAILED to take the node pin and package files from HEAD"
        rm -rf "$ctx"
        return 1
    fi
    chmod +x "$ctx/tools/fetch-node.sh"
    # node too, for the same reason: the pinned one, fetched and hash-checked
    # while the network is still on. The build then uses it, not Debian's.
    # TypeScript as well, from package-lock.json, into /opt/node-tools: the
    # tests type-check components/node-services with it.
    printf 'FROM debian:%s\nENV DEBIAN_FRONTEND=noninteractive\nRUN apt-get update && apt-get install -y --no-install-recommends %s && rm -rf /var/lib/apt/lists/*\nCOPY tools/node-version tools/fetch-node.sh /tmp/node/tools/\nRUN /tmp/node/tools/fetch-node.sh /opt/node-dist && rm -rf /tmp/node\nCOPY package.json package-lock.json /opt/node-tools/\nRUN cd /opt/node-tools && PATH=/opt/node-dist/current/bin:$PATH npm ci --ignore-scripts --no-audit --no-fund\n' \
        "$rel" "$(echo "$PACKAGES" | tr '\n' ' ')" \
        | "$RUNNER" build -t "$tag" -f - "$ctx" > "/tmp/webos-ci-image-$rel.log" 2>&1
    status=$?
    rm -rf "$ctx"
    if [ "$status" -ne 0 ]; then
        echo "  FAILED to build the image; see /tmp/webos-ci-image-$rel.log"
        # apt's own error is near the top of its output, not at the end: the tail
        # is docker repeating the RUN line back. Printing the tail hid
        # "E: Package 'libboost-system-dev' has no installation candidate"
        # behind three screens of the command being echoed.
        echo "  what apt actually said:"
        grep -hE "^(E|W): |Unable to locate|no installation candidate|Depends:|Conflicts:" \
            "/tmp/webos-ci-image-$rel.log" | sed 's/^/    /' | head -10
        echo "  (last lines, for anything apt did not report)"
        tail -3 "/tmp/webos-ci-image-$rel.log" | sed 's/^/    /'
        return 1
    fi
    echo "  ok"
}

run_target() {                  # run_target <release>
    local rel="$1" tag="webos-ce-ci:$rel"
    build_image "$rel" || return 1

    echo "== build and test on debian:$rel, with no network =="
    # git archive gives the committed tree and nothing else: no .git, no build/,
    # no editor droppings. Piped in, so nothing is written on the host.
    # On failure the logs matter more than the summary, and --rm would take them
    # with it. So the per-component logs under build/ are dumped before the
    # container exits: the first real failure this script found printed
    # "cjson FAILED" and nothing else, because the evidence died with the
    # container.
    #
    # Never pipe this into tail: the pipeline's status is tail's, and a failing
    # build then reports success. That mistake turned a red run green twice
    # while this was being written.
    # --no-sandbox below, and not as a shortcut: it is the only way inside an
    # unprivileged container. Chromium refuses to start as root without it
    # ("Running as root without --no-sandbox is not supported"), and running as
    # a non-root user does not help -- measured: the sandbox needs a user
    # namespace and Docker denies unshare even to uid 1000, the sid image has no
    # user between 1000 and 65533, and /src is not writable by one, so the
    # archive could not even be extracted.
    #
    # What it gives up is narrow: the sandbox isolates the renderer from hostile
    # content, and these tests load setHtml with strings written in this
    # repository, in a throwaway container with no network at all.
    #
    # --disable-gpu is repeated alongside it because qtwebkit-compat's
    # beforeApplication sets it only when QTWEBENGINE_CHROMIUM_FLAGS is empty,
    # so naming the variable here would otherwise drop it -- and without it
    # WebAppMgr dies with SIGSEGV at startup.
    #
    # These comments live above the pipeline, not inside it: a line continuation
    # cannot carry a comment, and putting them there is what broke this script.
    if ! git -C "$R" archive --format=tar HEAD \
        | "$RUNNER" run --rm -i --network none \
            -e QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --disable-gpu" \
            -e WEBOS_NODE_HOME=/opt/node-dist/current \
            -w /src "$tag" \
            sh -c 'mkdir -p /src && tar -x -C /src && \
                   ln -s /opt/node-tools/node_modules node_modules && \
                   { echo "--- build.sh ---" && tools/build.sh \
                     && echo "--- tests ---" \
                     && cmake -S tests -B build/tests > /tmp/t.log 2>&1 \
                     && cmake --build build/tests -j"$(nproc)" >> /tmp/t.log 2>&1 \
                     && ctest --test-dir build/tests --output-on-failure; } \
                   || { status=$?; \
                        echo "===== the failure ====="; \
                        # Only the logs that actually contain an error, newest
                        # first. Dumping every log in alphabetical order buried
                        # the real cause under successful installs of whatever
                        # sorts late -- pmloglib, pmstatemachineengine -- which
                        # is as useless as dumping none.
                        found=0; \
                        # /tmp/webos/*.log too: the node addon stage writes its
                        # log there and names it in the failure message, and a
                        # loop over build/*/*.log alone skipped the one file that
                        # mattered.
                        for l in $(ls -t build/*/*.log /tmp/webos/*.log /tmp/t.log 2>/dev/null); do \
                            [ -s "$l" ] || continue; \
                            # pkg-config says "None of the required X were found"
                            # and "No package X found" without ever using the
                            # word error, so a filter on "error" alone drops the
                            # one thing worth knowing: which module is missing.
                            # Seven components failed that way and the logs named
                            # none of them.
                            grep -qiE "error|undefined reference|No such file|Permission denied|cannot find|None of the required|No package .* found|Failed to find" "$l" || continue; \
                            echo "--- $l ---"; \
                            grep -niE "error|undefined reference|No such file|Permission denied|cannot find|None of the required|No package .* found|Failed to find" "$l" | head -12; \
                            found=1; \
                        done; \
                        [ "$found" = 1 ] || { echo "no log contains an error; newest logs:"; \
                            for l in $(ls -t build/*/*.log /tmp/t.log 2>/dev/null | head -3); do \
                                echo "--- $l (last 20) ---"; tail -20 "$l"; done; }; \
                        exit "$status"; }'
    then
        echo "  FAILED on $rel"
        return 1
    fi
    echo "  PASSED on $rel"
}

# What a caller wants out of a run, in one place. Grepping the log by hand for
# this has produced two empty reports already: the per-component lines are
# "<name>  OK" and "<name>  CONFIG FAILED", which match none of the obvious
# patterns like "^== " or "tests passed".
ci_summary() {                  # ci_summary <logfile>
    local log="$1"
    echo "--- components ---"
    grep -E "^[a-z0-9/-]+ +(OK|CONFIG FAILED|BUILD FAILED|INSTALL FAILED|FAILED)" "$log" || echo "  (none reported)"
    echo "--- stages, tests, verdict ---"
    grep -E "^== |rootfs assembled|tests passed|tests failed|Total Test time|PASSED on|FAILED on|^ci: " "$log" || true
}

failed=0
if [ $# -gt 0 ]; then
    for rel in "$@"; do run_target "$rel" || failed=1; done
else
    for rel in "${TARGETS[@]}"; do run_target "$rel" || failed=1; done
fi

echo
if [ "$failed" = 0 ]; then echo "ci: all targets passed"; else echo "ci: at least one target failed" >&2; fi
exit "$failed"
