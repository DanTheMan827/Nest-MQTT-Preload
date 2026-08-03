# Settings UI hook notes

## Result

A production-safe custom MQTT settings menu was not implemented from the supplied `nlclient` decompilation alone.

## What was found

- Existing settings persistence uses the imported `nlSettings` methods, including `HasValue`, `GetValue`, `SetValue`, and file write/load paths.
- Menu construction and localization paths are visible, including a learning menu builder around `FUN_000d3df8` and menu-model initialization around `FUN_00185e5c`.
- `nlclient` links against `libnlappkit.so`, `libnlapputils.so`, and related UI libraries.

## Missing evidence

No stable constructor/API was identified for all of the following:

1. Arbitrary text entry suitable for a broker hostname
2. Numeric entry with port validation
3. Masked password entry
4. Safe ownership/lifetime rules for injected menu rows
5. A persistent callback that can write custom `nlSettings` keys without corrupting the existing menu model

The likely controls live in external appkit libraries, which were not part of the supplied source export. Guessing C++ vtable layouts in a boot-critical settings screen presents an unacceptable boot-loop risk.

## Current configuration path

The library supports `/data/nest-mqtt.conf` plus `NEST_MQTT_*` environment variables. The file should be mode `0600` because it contains a plaintext MQTT password.

Suggested future native settings keys, once a UI path is proven:

```text
mqtt_enabled
mqtt_host
mqtt_port
mqtt_username
mqtt_password
mqtt_base_topic
mqtt_discovery_prefix
```

## Recommended next investigation

1. Decompile matching versions of `libnlappkit.so` and `libnlapputils.so`.
2. Find existing Wi-Fi SSID/password and postal-code editors, because they prove text and masked-text input semantics.
3. Identify the menu-item factory, callback ABI, ownership model, and localization lookup path.
4. Prototype a read-only “MQTT status” row first.
5. Add host and port editors next; add password only after masked input and secure persistence are verified.
6. Keep the file/environment configuration as a recovery fallback.

An optional UI hook should remain build-ID-gated independently from the cloud bucket hooks.
