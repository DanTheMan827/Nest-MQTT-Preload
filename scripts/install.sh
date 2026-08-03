#!/bin/sh
set -eu

PREFIX=${PREFIX:-/usr}
DATA_DIR=${DATA_DIR:-/data}
LIB_SRC=${1:-./libnest-mqtt-preload.so}

[ -f "$LIB_SRC" ] || { echo "missing $LIB_SRC" >&2; exit 1; }
install -d "$PREFIX/lib" "$PREFIX/bin" "$DATA_DIR"
install -m 0755 "$LIB_SRC" "$PREFIX/lib/libnest-mqtt-preload.so"
install -m 0755 scripts/run-nlclient.sh "$PREFIX/bin/nlclient-mqtt"
if [ ! -f "$DATA_DIR/nest-mqtt.conf" ]; then
    install -m 0600 config/nest-mqtt.conf.example "$DATA_DIR/nest-mqtt.conf"
fi
cat <<MSG
Installed. Edit $DATA_DIR/nest-mqtt.conf, then change the nlclient launch line to:
  $PREFIX/bin/nlclient-mqtt

Do not replace /bin/nlclient itself. Keep a serial/SSH recovery path before editing rcS.
MSG
