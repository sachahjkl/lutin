# Lua runtime

## Execution states

Lua 5.4.9 uses 32-bit numbers.
Separate states isolate persistent creations from short Code Mode tool scripts.
The native host owns the interface, filesystem access, networking, and input routing.

Creations define optional `init()`, `update(dt)`, and `draw()` callbacks.
The host supplies `dt = 1/60`. Real update frequency depends on total work.
The drawing and input API is documented in `docs/usage.md`.
Audio and sprites are not implemented.

Syntax is checked before replacing a running program.
A syntax failure preserves the previous creation.
A callback failure stops the affected creation and retains its diagnostic.

## Budgets

| Resource                      | Limit                                 |
| ----------------------------- | ------------------------------------- |
| Creation Lua state            | 2 MiB                                 |
| Code Mode state               | 512 KiB                               |
| Lua instructions per callback | 100,000                               |
| Pixel operations per callback | 262,144                               |
| Code Mode input               | 32 KiB                                |
| Tool result                   | 16 KiB, with a diagnostic on overflow |

Custom allocators reject excess memory requests.
Instruction hooks bound Lua work but do not preempt native C operations.
Exposed native functions have separate size or work bounds.
Pattern matching and unrestricted libraries are unavailable.
TLS and native allocations are separate from the Lua budgets.

## Verification

Host tests cover animation, infinite loops, memory exhaustion, malformed callbacks, native errors, and recovery.
Emulator checks cover the native ROM and scheduler behavior.
Sub-second stop latency and peak combined Lua/TLS memory remain physical-console measurements.
