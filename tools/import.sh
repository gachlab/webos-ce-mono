#!/bin/bash
# Imports each component with git subtree --squash, so the commit records the
# source repo and sha without dragging in the full history of 50 repos.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
imported=0; failed=0; skipped=0
while IFS=$'\t' read -r order name repo ref build; do
    [ "$order" = "orden" ] && continue
    [ "$repo" = "EXTERNO" ] && { echo "-- $name: external, not vendored"; skipped=$((skipped+1)); continue; }
    case "$name" in
        # Qt and WebKit are not vendored and not built any more: the build uses
        # Debian's Qt 6 and QtWebEngine.
        qt4|webkit) echo "-- $name: not vendored"; skipped=$((skipped+1)); continue;;
    esac
    [ -d "components/$name" ] && { echo "== $name: already imported"; skipped=$((skipped+1)); continue; }
    echo "++ $name  <-  $repo @ $ref"
    if git subtree add --prefix="components/$name" "https://github.com/$repo" \
         "refs/tags/$ref" --squash -m "import: $name from $repo @ $ref" >/dev/null 2>&1; then
        imported=$((imported+1))
    else
        echo "   FAILED $name"; failed=$((failed+1))
    fi
done < MANIFEST.tsv
echo
echo "imported: $imported   failed: $failed   skipped: $skipped"
