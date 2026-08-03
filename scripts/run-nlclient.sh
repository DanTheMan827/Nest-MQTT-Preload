#!/bin/sh
set -eu

LIB=${NEST_MQTT_LIBRARY:-/usr/lib/libnest-mqtt-preload.so}
NLCLIENT=${NLCLIENT_BINARY:-/bin/nlclient}
CONFIG=${NEST_MQTT_CONFIG:-/data/nest-mqtt.conf}

export NEST_MQTT_CONFIG="$CONFIG"
export LD_PRELOAD="$LIB${LD_PRELOAD:+:$LD_PRELOAD}"
exec "$NLCLIENT" "$@"
