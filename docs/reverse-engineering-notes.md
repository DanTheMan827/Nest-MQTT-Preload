# Reverse-engineering notes

## Exact firmware profile

| Item | Value |
|---|---|
| Program | `nlclient` |
| Architecture | ARM little-endian, 32-bit |
| ELF type | Executable, non-PIE |
| Image base | `0x00008000` |
| Build ID | `31ee6d0af8d98780f53872ab5e729cbd5243bff3` |
| Compiler marker | GCC 4.8.2 |

All internal addresses below are valid only for that build.

## Captured objects

| Object | Constructor | Size seen at allocation | Purpose |
|---|---:|---:|---|
| Shared/config cloud bucket (`nlCZBucket`) | `FUN_0009a6c4` at `0x0009a6c4` | `0x170` | Target state, range, mode, emergency heat, cloud synchronization |
| Device bucket | `FUN_0007e4fc` at `0x0007e4fc` | `0x270` | Current temperature, battery, capability and runtime state |

The ordinary client-global initialization allocates and registers both objects before pairing-specific cloud activity, so they are suitable local seams on paired and unpaired devices.

## Shared bucket pending fields

The native cloud apply routine was renamed `handle_cloud_data_maybe` in the Ghidra export. Its pending fields are 32-bit words from the bucket base:

| Word index | Byte offset | Meaning |
|---:|---:|---|
| `0x43` | `0x10c` | target temperature pending |
| `0x44` | `0x110` | target temperature, Q16.16 Celsius |
| `0x45` | `0x114` | range-low pending |
| `0x46` | `0x118` | range low, Q16.16 Celsius |
| `0x47` | `0x11c` | range-high pending |
| `0x48` | `0x120` | range high, Q16.16 Celsius |
| `0x49` | `0x124` | switch-over/HVAC mode pending |
| `0x4a` | `0x128` | native mode enum |
| `0x4b` | `0x12c` | emergency heat pending |
| `0x4c` | `0x130` | emergency heat boolean |
| `0x4d` | `0x134` | source string object |
| `0x54` | `0x150` | source identifier; zero becomes native source 3 |
| `0x58` | `0x160` | current target temperature, Q16.16 |

The apply routine invokes the client object through these vtable byte offsets:

| Offset | Operation |
|---:|---|
| `0x5c` | get manual eco mode |
| `0x58` | set manual eco mode |
| `0xb4` | set emergency heat |
| `0xb8` | get emergency heat |
| `0xbc` | set range high |
| `0xc0` | get range high |
| `0xc4` | set range low |
| `0xc8` | get range low |
| `0x108` | set switch-over/HVAC mode |
| `0x10c` | get switch-over/HVAC mode |

The target-temperature branch calls a native helper named `receive_temp_adjust_maybe` by the decompiler, preserving the same adjustment/event behavior as a cloud change.

## Locating the cloud apply callback

Ghidra renamed the central apply function, so its absolute address is not retained in the C export. The implementation finds it in the shared bucket vtable by locating these neighboring known entries:

- reset function `FUN_0009ad48` at `0x0009ad48`
- unknown apply entry immediately after reset
- post-apply function `FUN_0009b0e8` at `0x0009b0e8`

No unknown vtable entry is called unless the exact build ID matches.

## Direct fallback reads

### Device bucket

| Byte offset | Meaning |
|---:|---|
| `0x100` | current temperature, Q16.16 Celsius |
| `0x110` | battery-level field |
| `0x114` | current-reading valid flag |

### Shared/client objects

- Target temperature: shared bucket `+0x160`
- Range low/high, HVAC mode, emergency heat, and eco: client vtable getters listed above

Humidity and fan values were not assigned a direct getter with enough confidence, so they are sourced from flattened native bucket JSON.

## Passive cloud mirror

`nlLogWithComponent` is dynamically imported. Native bucket code logs serialized send/receive/update JSON, allowing a symbol interposer to flatten and publish all scalar properties without depending on cloud pairing. The interposer forwards the formatted message to the original logger and filters credential-like keys.

## Sleep path

The sleep manager sends `nlEventSystemSleep` to registered sleep-aware features, then calls the dynamically imported `nlWakeUp::Sleep()`. Exporting `_ZN8nlWakeUp5SleepEv` from the preload library blocks the final hardware sleep transition without rewriting the sleep manager's internal state machine.

## ARM inline hook format

Constructor hooks use an 8-byte ARM-mode absolute jump:

```text
E51FF004    ldr pc, [pc, #-4]
XXXXXXXX    destination address
```

A 16-byte executable trampoline copies the first 8 bytes and jumps to `original + 8`. Before hardware deployment, compare those original instructions against the target executable to ensure neither copied instruction is PC-relative.
