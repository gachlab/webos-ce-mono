#!/usr/bin/env python3
"""Turns a JavaScript file into a C array, the way "xxd -i" did.

HP's build used xxd, which comes with vim and is not always installed.

Usage:  embed-js.py <input.js> <output.h> <symbol>
"""
import sys

data = open(sys.argv[1], "rb").read()
rows = [", ".join("0x%02x" % b for b in data[i:i + 12]) for i in range(0, len(data), 12)]
open(sys.argv[2], "w").write(
    "unsigned char %s[] = {\n  " % sys.argv[3] + ",\n  ".join(rows) + "\n};\n"
    "unsigned int %s_len = %d;\n" % (sys.argv[3], len(data)))
