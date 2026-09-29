# Verification evidence

Evidence below distinguishes host tests, emulator checks, and physical-console feedback.
Automated checks use no API credentials.

## Reproduction commands

```sh
nix develop --command prek run --all-files
nix flake check "path:$PWD" --no-write-lock-file
nix build .#rom -o result-rom
nix build .#emulator-check -o result-emulator
nix build .#checks.x86_64-linux.network-emulator -o result-network-emulator
```

The flake pins the build inputs. Its default shell installs `prek` hooks.
Backlog tools were unavailable during this work. No `BACKLOG.json` was edited.

## Host checks

| Check       | Coverage                                                                                                                                                                                     |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `runtime`   | Drawing, syntax errors, invalid coordinates, instruction and memory budgets, native callback failures, and recovery.                                                                         |
| `workspace` | Partial writes, close errors, rename failures, rollback, stale backups, blocked temporary paths, and project selection.                                                                      |
| `agent`     | Tools, versions, sessions, Queue, Steer, cancellation, interrupted results, non-replay, and model persistence.                                                                               |
| `protocol`  | Responses and Chat Completions, fragmented arguments, reasoning, multiple calls, and output limits.                                                                                          |
| `network`   | Wi-Fi retries, idle reconnect, DNS worker cancellation, timeout, failure, successful resolution, and request reuse.                                                                          |
| `sse`       | Split fragments, CRLF, multiline events, and size limits.                                                                                                                                    |
| `chat`      | Bounded pagination, scrolling, recent-message tracking, and control-character filtering.                                                                                                     |
| `config`    | Defaults, model names, valid preferences, unknown keys, duplicate keys, invalid types, and oversized files.                                                                                  |
| `input`     | Press-and-release capture, one-time consumption, menu isolation, and retained touch coordinates.                                                                                             |
| `creation`  | Seeded input, paused frames, state inspection, budget failures, PNG captures, checkpoint recovery, modules, saves, tiles, animation, touch buttons, software triangles, and accented glyphs. |

Runtime, workspace, agent, protocol, network, chat, configuration, and input checks use AddressSanitizer and UndefinedBehaviorSanitizer.
Lua execution tests have external timeouts to detect native stalls.
Host storage failures do not simulate physical FAT corruption from power loss.
Creation checks also use AddressSanitizer and UndefinedBehaviorSanitizer.
Agent tests verify image request expansion, capture non-replay, and filename-only session persistence.
Protocol tests verify image content in both supported transports.

The creation-tool review identified six defects before release.
Regression cases cover restoration after more than 64 source files, backup-only files, and failed project selection.
They also cover exact inspection limits, initialization-only drawing, failed test replacement, and counted save-data access.

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

The `keyboard-emulator` check compares decompressed keyboard tiles with VRAM after initialization and during a real emulated touch press.
It also saves screenshots containing accented chat text and a pressed key.
The keyboard map starts at block 24, beyond the graphics allocation ending at `0xab80`.
This replaces block 20, which overlapped that allocation.

The scheduler probe compares BIOS VBlank waits with cooperative waits.
The measured result was zero background executions during 90 BIOS waits and 90 during 90 cooperative waits.
Lutin uses `cothread_yield_irq(IRQ_VBLANK)` so DSWiFi's receive thread can run.

The input probe samples through the same VBlank handler as the application.
It deliberately avoids consuming input for 120 VBlanks.
The automation presses and releases A during this interval, then checks that the press survives exactly one read.
This validates capture during a stalled consumer, not a universal response-time guarantee.

`scripts/check-network-emulator.sh` runs the production interface, network state machine, and resolver worker in a fault-injection ROM.
Linker wrappers report Wi-Fi association and stall the first DNS lookup for 30 seconds.
The test requires cancellation and menu input before the DNS function returns.
It checks that a second request cannot reuse a pending worker or consume its late result.
After the worker returns, it checks that a new request reaches the injected DNS failure.
The check stores screenshots, OCR output, and DNS entry/exit markers.
This covers scheduling and cancellation, not real packets, TLS, or authenticated inference.

Earlier emulator checks did not exercise a stalled DNS lookup.
The host libcurl build also differed from the console build, which disables the threaded resolver.
The resolver worker now keeps this blocking call outside the interface loop on both platforms.

A control run with the v0.5.2 network implementation fails the cancellation assertion during the injected DNS stall.
It leaves **WiFi:ON** and **Connecting to Wi-Fi...** on screen after B is pressed.
The patched host harness also downloaded the published catalog over verified HTTPS without an API key.

The `socket-emulator` check initializes the real DS network stack and exercises libcurl socket allocation and cleanup.
It injects connection refusal and unreachable-network errors across 24 attempts.
It verifies that diagnostics retain the socket error and resolved destination address.
This tests repeated failed connection setup, not successful TLS teardown or physical Wi-Fi reliability.

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

## Creation-tool sessions, 2026-09-29

GPT 6 Luna created Lantern Run (2D) and Signal Garden (software 3D) through the production host agent and tools.
Both sessions sent PNG captures as image input and recovered from model-generated code errors.
Both reached the 32-generation limit during visual edits, then completed after an explicit completion request.
A further targeted request corrected the 3D HUD contrast and camera pitch.

- The 2D test advanced 86 seeded frames and asserted movement, light collection, jumping, and landing.
- The 3D test asserted camera movement and an A-button mode change after 13 seeded frames.
- Both final sessions created a working checkpoint, resumed live input, and finished with a running creation.

`tests/demos/2d` and `tests/demos/3d` retain recorded responses, prior session history, and the generated source state.
The emulator recordings begin with that source and replay the final validation or correction calls.
They do not show a new live network request from the emulator.
The test observer compares tool success/failure with the live recording and requires a running creation with testing disabled.
It then requires real emulated RIGHT/A input to change creation state while frames advance.
The freeze regression confirms that a stopped main loop cannot pass that gameplay check.

The videos display `REPLAY` and use DS-mode emulation.
The v0.6.0 3D renderer used CPU rasterization. These results do not measure physical-DSi performance.
For the tested scene, the sampled DS-mode emulator frame time changed from 1,077,261 µs to 83,994 µs after rasterizer optimization.
This is one scene sample, not a guaranteed frame rate. The optimized run also recorded a 523,444 µs peak.
The final emulator logs report `E2E PLAY PASS` for both movement and A-button interaction.

## Hardware renderer, v0.7.0

The console uses the DS geometry engine for 2D primitives and 3D meshes.
Host tests retain the software reference renderer.
`hardware-emulator` checks pixels read through native display capture, including sprite transparency, layering, mesh depth, font glyphs, and clipping.
It also checks startup rollback, seeded startup state, geometry limits with clipping expansion, and VRAM cleanup after failed allocation.

The renderer review found missing clear-color rollback and missing clipping-expansion accounting.
A follow-up review found that removing the test framebuffer also removed the seeded startup input/time reset.
The hardware test includes regressions for these defects.

The same recorded 2D/3D agent sessions now execute against the GPU backend in the emulator.
The `REPLAY GPU` label identifies this backend; historical conversation text still describes the earlier software renderer.
The gameplay observer still requires input-driven state changes and continued frame progression.
The freeze and inspection-failure regressions remain required checks.

An intermediate optimized run sampled 6,168 µs of 3D CPU frame work, compared with 83,994 µs in v0.6.0.
Its last completed main loop took 16,711 µs, including 6,298 µs of active work.
That loop copied zero console-map bytes and skipped zero VBlanks.
These are DS-mode emulator samples from one scene, not physical-console measurements or guaranteed frame rates.

## Physical-console feedback

The user confirmed DSi-mode startup, then Wi-Fi association after enabling `WIFI_ATTEMPT_DSI_MODE`.
Correcting the console clock resolved a certificate-validity failure.
The user subsequently confirmed that the agent worked on their DSi XL.
Later feedback reported a receive abort during inference and an unresponsive catalog update.
The DNS-worker change still requires a physical-console retest; it does not establish the cause of the receive abort.

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
