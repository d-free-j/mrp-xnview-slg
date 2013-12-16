#!/bin/sh

# mingw
DEST=Xslg.usr
HOST=i586-mingw32msvc-

SRC="src/zlib/inflate.c src/zlib/crc32.c src/zlib/adler32.c src/zlib/zutil.c src/zlib/inftrees.c src/zlib/inffast.c src/slgplugin.c"

${HOST}gcc -O2 -Wall -fPIC -shared -o $DEST -Isrc/zlib -finput-charset=UTF-8 $SRC -Wl,-kill-at

