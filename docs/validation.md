# Validation performed

The following checks were completed after replacing the handwritten protocol code:

1. Complete shared-library syntax/link build under GNU C++98-compatible flags using API-compatible dependency stubs.
2. Dynamic export verification for:
   - `nlLogWithComponent`
   - `_ZN8nlWakeUp5SleepEv` (`nlWakeUp::Sleep()`)
3. Strict C99 syntax validation of the Nest MQTT-C platform adapter.
4. Strict C++98 syntax validation of the MQTT-C wrapper and picojson integration.
5. Review against the pinned MQTT-C public API and callback behavior, including:
   - nonblocking socket requirements
   - CONNECT/CONNACK queue lifecycle
   - callback invocation while the MQTT client mutex is held
   - deferred Home Assistant rediscovery to avoid callback reentrancy
6. Dependency URLs and license paths verified at the pinned commits.

The GitHub Actions workflow performs the remaining reproducible checks with the actual pinned sources:

- `make deps`
- picojson unit tests
- ARM cross-compilation with the Nest toolchain
- preload export verification

Not yet validated in this environment:

- Actual MQTT-C and picojson source compilation, because the local execution container has no outbound network access
- End-to-end broker integration after the MQTT-C replacement
- Runtime behavior on physical thermostat hardware
- ARM trampoline instructions against the original binary bytes
- Native command thread affinity
- Inferred HVAC mode and eco enum semantics
- MQTT broker authentication against a production broker
