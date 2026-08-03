# Validation performed

The following checks were completed in the development environment:

1. Host build of the complete shared library with `-std=gnu++11`, `-fPIC`, and `--no-undefined`.
2. Dynamic export verification for:
   - `nlLogWithComponent`
   - `_ZN8nlWakeUp5SleepEv` (`nlWakeUp::Sleep()`)
3. JSON extraction/flattening unit test, including nested properties, arrays, and sensitive-key filtering.
4. `LD_PRELOAD` smoke test against `/bin/true`; the non-ARM build correctly entered passive mode and exited cleanly.
5. Fake MQTT 3.1.1 broker integration test, which verified:
   - CONNECT/CONNACK
   - Command-topic SUBSCRIBE
   - Retained Home Assistant climate discovery payload
   - Retained battery sensor discovery payload
   - Valid JSON for both discovery messages
   - Retained `online` availability
   - Retained `offline` availability on graceful shutdown
   - MQTT DISCONNECT

Not yet validated:

- Cross-compilation with the external Nest toolchain in this environment
- ARM trampoline instructions against the original binary bytes
- Runtime behavior on physical thermostat hardware
- Native command thread affinity
- Inferred HVAC mode and eco enum semantics
- MQTT broker authentication against a production broker
