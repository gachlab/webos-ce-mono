#!/bin/bash
# Ajustes de layout que el componente NO hace por si mismo y que el script de HP
# aplicaba a mano despues de 'make install'. Sus consumidores incluyen la cabecera
# de dos formas distintas -- <lunaservice.h> y <luna-service2/lunaservice.h> --
# asi que tiene que estar en ambos sitios.
S="$1"
mkdir -p "$S/include/luna-service2" "$S/lib"
cp -f "$S/usr/include/luna-service2/lunaservice.h"        "$S/include/"
cp -f "$S/usr/include/luna-service2/lunaservice-errors.h" "$S/include/luna-service2/"
cp -f "$S/usr/include/luna-service2/lunaservice.h"        "$S/usr/include/"
cd "$S/lib" || exit 1
ln -sf ../usr/lib/libluna-service2.so libluna-service2.so
ln -sf ../usr/lib/libluna-service2.so libluna-service2.so.3
ln -sf ../usr/lib/libluna-service2.so liblunaservice.so
