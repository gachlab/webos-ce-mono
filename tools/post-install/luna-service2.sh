#!/bin/bash
# Layout fixups the component does NOT do itself and that HP's script applied by
# hand after 'make install'. Its consumers include the header two different ways
# -- <lunaservice.h> and <luna-service2/lunaservice.h> -- so it has to exist in
# both places.
S="$1"
mkdir -p "$S/include/luna-service2" "$S/lib"
cp -f "$S/usr/include/luna-service2/lunaservice.h"        "$S/include/"
cp -f "$S/usr/include/luna-service2/lunaservice-errors.h" "$S/include/luna-service2/"
cp -f "$S/usr/include/luna-service2/lunaservice.h"        "$S/usr/include/"
cd "$S/lib" || exit 1
ln -sf ../usr/lib/libluna-service2.so libluna-service2.so
ln -sf ../usr/lib/libluna-service2.so libluna-service2.so.3
ln -sf ../usr/lib/libluna-service2.so liblunaservice.so
