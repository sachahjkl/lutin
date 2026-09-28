# Verification evidence

Evidence below distinguishes host tests, emulator checks, and physical-console feedback.
Automated checks use no API credentials.

## Reproduction commands

```sh
nix develop --command prek run --all-files
nix flake check "path:$PWD" --no-write-lock-file
nix build .#rom -o result-rom
nix build .#emulator-check -o result-emulator
```

The flake pins the build inputs. Its default shell installs `prek` hooks.
Backlog tools were unavailable during this work. No `BACKLOG.json` was edited.

## Host checks

| Check       | Coverage                                                                                                                |
| ----------- | ----------------------------------------------------------------------------------------------------------------------- |
| `runtime`   | Drawing, syntax errors, invalid coordinates, instruction and memory budgets, native callback failures, and recovery.    |
| `workspace` | Partial writes, close errors, rename failures, rollback, stale backups, blocked temporary paths, and project selection. |
| `agent`     | Tools, versions, sessions, Queue, Steer, cancellation, interrupted results, non-replay, and model persistence.          |
| `protocol`  | Responses and Chat Completions, fragmented arguments, reasoning, multiple calls, and output limits.                     |
| `network`   | DSi Wi-Fi flags, bounded association retries, idle reconnect, and cancellation.                                         |
| `sse`       | Split fragments, CRLF, multiline events, and size limits.                                                               |
| `chat`      | Bounded pagination, scrolling, recent-message tracking, and control-character filtering.                                |
| `config`    | Defaults, model names, valid preferences, unknown keys, duplicate keys, invalid types, and oversized files.             |
| `input`     | Press-and-release capture, one-time consumption, menu isolation, and retained touch coordinates.                        |

Runtime, workspace, agent, protocol, network, chat, configuration, and input checks use AddressSanitizer and UndefinedBehaviorSanitizer.
Lua execution tests have external timeouts to detect native stalls.
Host storage failures do not simulate physical FAT corruption from power loss.

Session tests create fifteen sessions and verify pagination, reset, deletion, recreation, and failed-switch recovery.
They verify that reset retains the selected model and project files.
The FAT rename shim reproduces the console behavior that rejects an existing destination.

## Emulator checks

The `reproducible-rom` check rebuilds with reversed source-file discovery and compares the binary with the normal ROM.
The Makefile sorts the C source list before the SDK constructs its object list.
This removes the filesystem-order difference observed between local and GitHub runner builds.

`scripts/check-emulator.sh` launches the built ROM in melonDS under Xvfb.
It checks chat, keyboard, START menu, program launch, return from gameplay, and model selection through screen captures and OCR.
The emulator uses isolated configuration and a writable ROM copy.
It runs in DS mode with built-in BIOS replacements, not full DSi emulation.

The scheduler probe compares BIOS VBlank waits with cooperative waits.
The measured result was zero background executions during 90 BIOS waits and 90 during 90 cooperative waits.
Lutin uses `cothread_yield_irq(IRQ_VBLANK)` so DSWiFi's receive thread can run.

The input probe samples through the same VBlank handler as the application.
It deliberately avoids consuming input for 120 VBlanks.
The automation presses and releases A during this interval, then checks that the press survives exactly one read.
This validates capture during a stalled consumer, not a universal response-time guarantee.

## Authenticated host inference, 2026-09-28

DeepSeek V4 Flash generated an animation through the real host agent harness.
It corrected a floating-coordinate error and finished with a running Lua state.
The process returned zero. Its temporary credential file was removed.

A separate platformer request reproduced the reported failures.
The original run stopped after roughly 82 seconds and 524,268 SSE bytes without executing a tool.
The transport limit counted repeated SSE envelopes, not just generated code.
The progress display also overwrote the limit diagnostic.

An intermediate run exposed invented versions and empty `expected_version` values.
The schemas now require `missing` for absent files or the exact returned hexadecimal version.
Writes and patches return the next version.
Code Mode now loads the advertised `math` and `table` libraries.

The corrected run completed in 115.7 seconds:

- 32 tool calls, including two rejected calls that the model corrected.
- No stale-version errors in that run.
- A 2,917-byte final program.
- A largest written text block of 1,036 characters.
- A running runtime with no final Lua error.

The two corrected failures were a missing `path` argument and a non-unique exact replacement.
The original request asked, in French, for a Mario-style platformer level.
These results use host networking, not emulated DSi networking.

## Physical-console feedback

The user confirmed DSi-mode startup, then Wi-Fi association after enabling `WIFI_ATTEMPT_DSI_MODE`.
Correcting the console clock resolved a certificate-validity failure.
The user subsequently confirmed that the agent worked on their DSi XL.

The latest input-buffer changes still require physical-console feedback.
Full DSi emulation, maximum combined Lua/TLS memory, native-operation latency, and power-loss behavior remain unmeasured.

## Presentation video

`docs/media/presentation.mp4` records a running session in melonDS.
The presentation build replays responses from an authenticated DeepSeek request, with the actual agent and tools executing in the ROM.
The session writes a greeting program, detects undefined button constants, patches them, reruns the program, and checks its runtime.
The replay transport displays `REPLAY` and is compiled only into `presentation-rom`.
The normal release ROM uses HTTPS.
The GIF is a reduced-frame-rate preview of the same recording.

Rebuild it with:

```sh
nix build .#presentation-rom -o result-presentation
nix develop --command bash scripts/record-presentation.sh result-presentation/lutin.nds /tmp/opencode/lutin-presentation
```

## Installation kit

Nix verifies the pinned downloads in `nix/installation-sources.json`.
The kit contains TWiLight Menu++ v27.24.1, Safe Unlaunch installer v2.6, dumpTool v1.0, Memory Pit, dsibiosdumper, and sdFormatLinux v0.2.0.
It also contains the current ROM, starter animation, configuration example, and public CA bundle.
It contains no API key or console dump.

## Project discovery and media

Workspace tests create more than twelve projects and open project 1234.
They verify sorted pagination and rejection of non-directory entries and noncanonical identifiers.
Runtime tests verify sprite transparency, clipping, scaling, flipping, and malformed assets.
Audio tests verify note timing, rests, looping, voice constraints, and cleanup.
They also verify that a rejected program replacement preserves active audio.
The `media` check loads the bundled Lua assets through the real workspace reader.
It renders animation and checks music/effect events through a host audio callback.
Physical-console sprite controls and audible output remain unverified.

## SD model catalog

The `catalog` check validates provider/model parsing, personal entries, defaults, duplicate rejection, failed replacement, and FAT backup recovery.
The generator test checks route selection and rejects models without text/tool support.
Agent tests remove a selected model and verify that its queued request remains pending.
Network tests verify that a catalog update starts without a provider key and can be cancelled.
The emulator loads the SD catalog and invokes **Reload model catalog** through the command menu.
Physical-console catalog download remains unverified.

## Published release

Release `v0.3.0` passed GitHub CI and published the ROM, SD ZIP, and `SHA256SUMS`.
Downloaded checksums passed. The CI ROM matched the local ROM byte-for-byte.
Its ROM SHA-256 is `68280b69d71e430cd5ad9650ec66a47c5d204b9be3369f69b9d6764dd24e77e6`.
Dynamic projects and media APIs are later source changes.
