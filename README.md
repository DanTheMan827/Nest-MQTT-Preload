# nest-mqtt-preload

An experimental, dependency-free `LD_PRELOAD` library for the 32-bit ARM `nlclient` binary used by older Nest thermostats. It mirrors thermostat state to MQTT, publishes Home Assistant MQTT discovery, routes supported commands through the same local setters used by the native cloud bucket, and interposes the final hardware sleep entry point while loaded.

## Firmware target and safety model

The native hook profile is intentionally restricted to this exact executable:

- ELF build ID: `31ee6d0af8d98780f53872ab5e729cbd5243bff3`
- ARM little-endian, 32-bit, non-PIE image
- Original image base: `0x00008000`
- Observed compiler: GCC 4.8.2

On any other build ID, the library refuses all absolute-address hooks and native writes. MQTT log mirroring and the symbol-level sleep interposer remain available. This avoids blindly patching a different firmware build.

**This has been source-reviewed and host-compiled, but not tested on a physical thermostat. Keep a serial/SSH recovery path and a known-good `rcS` backup.**

## What it exposes

Home Assistant receives one MQTT climate entity with:

- Current temperature and humidity when available
- Target temperature, range low, and range high
- HVAC mode: `off`, `heat`, `cool`, `auto`
- HVAC action inferred from native heater, compressor, and fan state fields
- Eco preset
- Availability

Every non-sensitive scalar seen in native cloud bucket JSON is also retained under:

```text
<base_topic>/property/<flattened-property-path>
```

Sensitive names containing `password`, `secret`, `credential`, `token`, or private-key terms are discarded before publication. See `docs/property-map.md` for the complete property inventory found in the decompilation.

## Native command path

Commands use the native `nlCZBucket` apply path rather than writing HVAC state directly:

```text
<base_topic>/set/target_temperature
<base_topic>/set/target_temperature_low
<base_topic>/set/target_temperature_high
<base_topic>/set/hvac_mode
<base_topic>/set/emergency_heat
<base_topic>/set/preset_mode
```

The shared cloud/config bucket is constructed during ordinary `nlclient` initialization whether or not the thermostat is paired. The preload library captures that bucket constructor and stages values into its pending-change fields before invoking the bucket's existing apply callback. Consequently, the same command path is available on paired and unpaired devices for the supported firmware build.

The inferred native HVAC mode mapping is:

| MQTT mode | Native value |
|---|---:|
| `heat` | 0 |
| `cool` | 1 |
| `auto` | 2 |
| `off` | 3 |

Native value 4 appears to be heat with emergency heat handled by a separate flag. Verify this mapping on a bench thermostat before enabling automations.

## Sleep prevention

`nlclient` imports the C++ symbol `nlWakeUp::Sleep()`. The library exports the same symbol and returns without entering hardware sleep whenever `block_sleep=true`. This leaves the native sleep manager intact and intercepts only its final sleep call.

Disable this behavior with:

```ini
block_sleep=false
```

or environment variable:

```sh
NEST_MQTT_BLOCK_SLEEP=false
```

## Build

This project follows the toolchain convention used by `Nest-Spare-Key`:

```sh
git submodule update --init --recursive
export CROSS="$TOOLCHAIN_CROSS-"
make -j$(nproc) all check-exports
make strip
```

The included GitHub Actions workflow uses `cuckoo-nest/toolchain@main`, builds with `CROSS=${TOOLCHAIN_CROSS}-`, uploads unstripped and stripped artifacts, and creates releases for `v*` tags.

A normal host compiler can validate the parser and non-ARM portions:

```sh
make host-test
make all check-exports
```

## Configure

Copy the example and protect the password:

```sh
cp config/nest-mqtt.conf.example /data/nest-mqtt.conf
chmod 600 /data/nest-mqtt.conf
```

Environment variables override file values. Supported overrides include:

```text
NEST_MQTT_CONFIG
NEST_MQTT_HOST
NEST_MQTT_PORT
NEST_MQTT_USERNAME
NEST_MQTT_PASSWORD
NEST_MQTT_CLIENT_ID
NEST_MQTT_BASE_TOPIC
NEST_MQTT_DISCOVERY_PREFIX
NEST_MQTT_DEVICE_NAME
NEST_MQTT_DEVICE_ID
NEST_MQTT_NATIVE_WRITES
NEST_MQTT_BLOCK_SLEEP
```

MQTT is currently plain TCP only. Put the thermostat and broker on a trusted network or terminate TLS through a local proxy.

## Install and launch

```sh
make
./scripts/install.sh ./libnest-mqtt-preload.so
vi /data/nest-mqtt.conf
```

Launch `nlclient` through the wrapper:

```sh
/usr/bin/nlclient-mqtt
```

The wrapper defaults to `/bin/nlclient`. Override an alternate path with `NLCLIENT_BINARY`.

Do not overwrite the original `nlclient`. Edit the startup script only after making a recovery copy. The essential launch form is:

```sh
NEST_MQTT_CONFIG=/data/nest-mqtt.conf \
LD_PRELOAD=/usr/lib/libnest-mqtt-preload.so \
exec /bin/nlclient
```

## Settings UI status

A custom MQTT menu is not enabled. The decompiled executable identifies menu assembly and localization paths, but no trustworthy password/text-entry constructor was found; those controls appear to be implemented in external `libnlappkit`/appkit code not included in the supplied decompilation. `docs/ui-hook-notes.md` records the candidates and a safe path for future work.

## Known limitations

- Physical-device testing is still required.
- Native absolute-address hooks support only the listed build ID.
- The ARM trampoline assumes the first two instructions of each hooked constructor are position-independent prologue instructions; verify original bytes before deployment.
- Native writes are invoked from the MQTT worker thread. The cloud apply path mostly queues native events, but thread affinity has not been proven.
- Humidity, HVAC action, fan state, and many capability fields depend on observed cloud/bucket JSON when no verified direct getter was found.
- Fan control is read-only because no setter was mapped with sufficient confidence.
- No MQTT TLS support is included.
