#!/bin/sh
# PORTABILITY PATCH -- not from HP. Already applied to the vendored source:
# this script is kept because it is the explanation of that difference, which
# `git diff hp-original -- components/luna-universalsearchmgr` shows. Nothing
# runs it during a build.
#
# HP casts the format string to (const xmlChar*):
#     xmlStrPrintf (buf, 1024, (const xmlChar*) "&%s=%s", name, value);
# while libxml2 declares:
#     int xmlStrPrintf(xmlChar *buf, int len, const char *msg, ...);
# HP built against its own libxml2, whose signature took const xmlChar*.
# Anywhere else that is an error, not a warning. Three calls, all in
# OpenSearchHandler.cpp.
SRC="$1/Src/OpenSearchHandler.cpp"
[ -f "$SRC" ] || { echo "$SRC does not exist"; exit 1; }
sed -i 's/(const xmlChar[*]) "?%s=%s"/(const char*) "?%s=%s"/; s/(const xmlChar[*]) "&%s=%s"/(const char*) "\&%s=%s"/; s/(const xmlChar[*]) "%s=%s"/(const char*) "%s=%s"/' "$SRC"
echo "libxml2 patch applied: $(grep -c "xmlStrPrintf (buf, 1024, (const char\*)" "$SRC") of 3 calls converted"
