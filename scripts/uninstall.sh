#!/bin/sh
set -eu
PREFIX=${PREFIX:-/usr}
rm -f "$PREFIX/lib/libnest-mqtt-preload.so" "$PREFIX/bin/nlclient-mqtt"
echo "Library and wrapper removed. Restore the original nlclient launch line manually."
