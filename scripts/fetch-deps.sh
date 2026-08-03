#!/bin/sh
set -eu

MQTT_C_COMMIT=7a986a68ebea63921d4aab20a9d1b26a8b5f8c9d
PICOJSON_COMMIT=111c9be5188f7350c2eac9ddaedd8cca3d7bf394
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

fetch() {
    url=$1
    output=$2
    tmp=${output}.tmp
    mkdir -p "$(dirname -- "$output")"
    if command -v curl >/dev/null 2>&1; then
        curl --fail --location --retry 3 --silent --show-error "$url" -o "$tmp"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$tmp" "$url"
    else
        echo "curl or wget is required to fetch pinned dependencies" >&2
        exit 1
    fi
    mv "$tmp" "$output"
}

mqtt_base="https://raw.githubusercontent.com/LiamBindle/MQTT-C/${MQTT_C_COMMIT}"
pico_base="https://raw.githubusercontent.com/kazuho/picojson/${PICOJSON_COMMIT}"

fetch "$mqtt_base/include/mqtt.h" "$ROOT/third_party/mqtt-c/include/mqtt.h"
fetch "$mqtt_base/src/mqtt.c" "$ROOT/third_party/mqtt-c/src/mqtt.c"
fetch "$mqtt_base/LICENSE" "$ROOT/third_party/mqtt-c/LICENSE"

fetch "$pico_base/picojson.h" "$ROOT/third_party/picojson/picojson.h"
fetch "$pico_base/LICENSE" "$ROOT/third_party/picojson/LICENSE"

cat > "$ROOT/third_party/.versions" <<VERSIONS
MQTT-C $MQTT_C_COMMIT
picojson $PICOJSON_COMMIT
VERSIONS
