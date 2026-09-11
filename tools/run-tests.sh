#!/bin/bash
# Builds and runs the tests. They are headless and take under a second, so there
# is no reason to bring the shell up to find out whether something works.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT/tests" -B "$ROOT/build-modern/tests" >/dev/null
cmake --build "$ROOT/build-modern/tests" -j"$(nproc)" >/dev/null
ctest --test-dir "$ROOT/build-modern/tests" --output-on-failure "$@"
