# Technical decisions and evidence

## Console SDK

BlocksDS provides DSi Wi-Fi support, libcurl, Mbed TLS, and documented HTTPS examples.
The pinned `blocksds-nix` input adapts its toolchain to Nix.
devkitPro was considered, but BlocksDS already supplied the required Nix integration and networking examples.

Sources:

- <https://blocksds.skylyrac.net/tutorial/advanced/wifi/>
- <https://blocksds.skylyrac.net/docs/guides/faq/>
- <https://github.com/pgattic/blocksds-nix>

## Console deployment

The target is a `.nds` homebrew launched from the console SD slot in DSi mode.
The selected setup is Unlaunch and TWiLight Menu++.
DSi-mode Wi-Fi initialization explicitly requests `WIFI_ATTEMPT_DSI_MODE`.
The user confirmed working inference after the Wi-Fi scheduling and clock fixes.

Sources:

- <https://dsi.cfw.guide/>
- <https://blocksds.skylyrac.net/docs/guides/filesystem/>

## AI transport

OpenCode Go supplies direct HTTPS inference.
GPT 6 Luna, GPT 5.6 Luna, and Grok 4.6 use Responses.
DeepSeek V4 Flash uses Chat Completions with low reasoning effort.
The adapter preserves reasoning content required by subsequent DeepSeek tool turns.
The service contract is in `contracts/service.md`.

The platformer regression showed that raw SSE traffic greatly exceeds decoded code size.
Transport and decoded-output budgets are therefore separate.
Explicit JSON tools reduce the need for models to generate Lua merely to edit files.
Versioned chunks, paged reads, and exact patches bound individual operations.

Sources:

- <https://opencode.ai/v2/docs/console/go/>
- <https://api-docs.deepseek.com/guides/thinking_mode>

## Sandbox

Lua supplies an established embeddable language rather than a custom command language.
Separate Lua states isolate persistent creations from short tool scripts.
Custom allocators enforce memory budgets. Instruction hooks bound Lua work.
Native operations require their own bounds because instruction hooks do not preempt C code.
Pattern functions are unavailable for that reason.

Source: <https://www.lua.org/manual/5.4/manual.html>.

## Input and scheduling

BlocksDS cooperative threads require scheduler-aware waits.
The scheduler regression measured no worker progress during BIOS waits and one worker execution per cooperative VBlank wait.
Sampling buttons only in the main loop loses short taps during expensive work.
The VBlank handler now retains edges independently of the consumer.
Host tests cover state transitions. An emulator probe verifies a tap during a two-second consumer stall.

The SDK input and keyboard implementations were inspected before choosing interrupt sampling.
The keyboard helper reads the held touch state. Physical-button buffering is owned by Lutin.

## Installation and emulation

melonDS runs the DS-mode checks with built-in replacement BIOS support.
DSi emulation needs console-owned BIOS, firmware, and NAND data.
Those files are excluded from the repository and build inputs.

The kit pins upstream installation files in `nix/installation-sources.json`.
The official guide remains the authority for console modification steps.

Sources:

- <https://dsi.cfw.guide/installing-unlaunch.html>
- <https://wiki.ds-homebrew.com/twilightmenu/installing-dsi>
- <https://github.com/melonDS-emu/melonDS>
