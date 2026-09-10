#!/bin/sh
# PARCHE DE PORTABILIDAD -- no viene de HP.
#
# HP castea la cadena de formato a (const xmlChar*):
#     xmlStrPrintf (buf, 1024, (const xmlChar*) "&%s=%s", name, value);
# pero la libxml2 de Ubuntu 12.04 (2.7.8) declara:
#     int xmlStrPrintf(xmlChar *buf, int len, const char *msg, ...);
# HP compilaba contra su propia libxml2, cuya firma tomaba const xmlChar*.
# Con gcc 4.6 esto es error, no warning. Son 3 llamadas en OpenSearchHandler.cpp.
SRC="$1/Src/OpenSearchHandler.cpp"
[ -f "$SRC" ] || { echo "no existe $SRC"; exit 1; }
sed -i 's/(const xmlChar[*]) "?%s=%s"/(const char*) "?%s=%s"/; s/(const xmlChar[*]) "&%s=%s"/(const char*) "\&%s=%s"/; s/(const xmlChar[*]) "%s=%s"/(const char*) "%s=%s"/' "$SRC"
echo "parche libxml2 aplicado: $(grep -c "xmlStrPrintf (buf, 1024, (const char\*)" "$SRC") de 3 llamadas convertidas"
