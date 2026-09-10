#!/bin/bash
# Importa cada componente con git subtree --squash, para que el commit registre
# el repo y el sha de origen sin arrastrar la historia completa de 50 repos.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
ok=0; fallo=0; salt=0
while IFS=$'\t' read -r orden name repo ref build; do
    [ "$orden" = "orden" ] && continue
    [ "$repo" = "EXTERNO" ] && { echo "-- $name: externo, no se vendorea"; salt=$((salt+1)); continue; }
    case "$name" in
        qt4|webkit) echo "-- $name: third-party pineado, no se vendorea"; salt=$((salt+1)); continue;;
    esac
    [ -d "components/$name" ] && { echo "== $name: ya importado"; salt=$((salt+1)); continue; }
    echo "++ $name  <-  $repo @ $ref"
    if git subtree add --prefix="components/$name" "https://github.com/$repo" \
         "refs/tags/$ref" --squash -m "import: $name desde $repo @ $ref" >/dev/null 2>&1; then
        ok=$((ok+1))
    else
        echo "   FALLO $name"; fallo=$((fallo+1))
    fi
done < MANIFEST.tsv
echo
echo "importados: $ok   fallidos: $fallo   saltados: $salt"
