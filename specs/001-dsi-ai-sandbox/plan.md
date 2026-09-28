# Implementation plan: Lutin

**Feature:** `001-dsi-ai-sandbox`
**Updated:** 2026-09-28
**Status:** native coding agent implemented, publication and hardware validation in progress.

## Product direction

Lutin is an AI coding agent for Nintendo DSi.
The AI creates interactive Lua programs through tools. The console runs them locally.
The selected launch path is the console SD slot with Unlaunch and TWiLight Menu++ in DSi mode.
The user has tested the agent on a DSi XL.

```text
DSi interface → agent → protocol adapter → HTTPS → OpenCode Go
                  ↓
            file/runtime tools → local Lua creation → lower screen
                  ↓
             SD project and session journal
```

## Implemented layers

1. BlocksDS toolchain, native ROM, Nix development shell, and automated checks.
2. Lua 5.4.9 with 32-bit numbers, drawing/input API, and per-state budgets.
3. Persistent project storage with FAT-safe replacement and backup recovery.
4. Autonomous tools, session journals, Queue, Steer, and interrupted-call recovery.
5. Colored upper-screen chat and persistent lower-screen keyboard, preview, or gameplay.
6. OpenCode Go models with separate Responses and Chat Completions adapters.
7. Dynamic sessions, configuration file, versioned write chunks, and paged reads.
8. VBlank input capture and menu isolation, with host and emulator regressions.

## Technical decisions

Provider definitions and model metadata load from `/lutin/models.json`.
`models.local.json` supplies persistent personal overrides by ID.
The command menu reloads local files or downloads a bounded compact catalog generated from models.dev and verified transport routes.
Validation and FAT replacement preserve the previous catalog on failure.

| Area           | Decision                                                         |
| -------------- | ---------------------------------------------------------------- |
| Console SDK    | BlocksDS/libnds and DSWiFi.                                      |
| Network        | libcurl with Mbed TLS and certificate verification.              |
| Service        | Provider registry; OpenCode Go is the bundled provider.          |
| Default model  | GPT 6 Luna. DeepSeek V4 Flash uses Chat Completions.             |
| Credentials    | `/lutin/keys/<provider-id>`, loaded at runtime.                  |
| Configuration  | `/lutin/config.json`, strict validation with atomic rejection.   |
| Language       | Lua 5.4.9, separate creation and Code Mode states.               |
| Rendering      | RAM framebuffer and terminal maps, copied to VRAM at VBlank.     |
| Scheduling     | Cooperative VBlank waits keep the DSWiFi receive thread active.  |
| Input          | VBlank sampling retains press edges until main-loop consumption. |
| Persistence    | Flat project files and one JSON document per session.            |
| Model identity | Persist the provider-qualified `provider/model` ID.              |
| Publication    | MIT, English documentation, local logo and presentation assets.  |

## Interface

The upper screen displays the conversation and status.
The lower screen displays the keyboard, preview, game controls, or selected list.
START opens a modal menu. Only Play controls routes input to a creation.
The draft survives view switches. Stop agent and Stop program are independent.

Sessions are discovered on SD and listed in bounded pages.
Create, switch, reset, and delete do not depend on predefined session slots.
Reset retains the selected model and project files.
Projects are discovered from numbered SD directories and listed in bounded pages.
Users create projects on demand without predefined slots.
The runtime compiles palette sprites and note sequences from editable Lua assets.
Asset loading uses the project path restrictions and runs only during startup or `init`.
Three square-wave voices and one noise voice provide synthesized music and effects through libnds.

## Input behavior

The interrupt captures button state and touch coordinates without executing tools or drawing.
The main loop handles input before runtime and agent work.
Agent work skips an iteration when a new press is handled.
Only keys held in the menu are blocked when returning to play, until each key is released.
Short taps remain visible for one update. Multiple same-key taps during a stall coalesce.
This does not preempt TLS, filesystem calls, or other long native operations.

DNS lookup runs in one worker with private input and result storage.
The interface polls for completion and supplies the resolved address to libcurl.
Cancellation and the DNS deadline detach the request from the pending lookup.
The worker finishes normally before another lookup starts; it is never killed inside the system resolver.
The DS worker uses a 16 KiB stack and the BlocksDS cooperative scheduler.
Host builds use a POSIX thread for the same lookup code.

## Checks

`flake.nix` exposes ROM, installation kit, runtime, workspace, agent, network, protocol, SSE, chat, config, input, hooks, and emulator checks.
Host behavior tests use sanitizers where applicable.
The emulator uses DS-mode BIOS replacements and requires no private files.
The input test deliberately stalls the consumer while sending a short button press.
Authenticated host inference is a separate manual check using a temporary credential file.
The network emulator check runs the production interface, network state machine, and resolver worker with injected Wi-Fi and DNS functions.
It stalls DNS for 30 seconds and checks cancellation, menu input, late-result isolation, and retry after failure.
It does not validate real Wi-Fi packets, TLS negotiation, or inference on a physical DSi.

## Remaining validation and capabilities

- Measure native-operation latency and combined Lua/TLS memory on a physical DSi.
- Confirm the new input behavior during console gameplay and live requests.
- Test power loss during FAT writes on hardware.
- Validate full DSi emulation with user-owned BIOS, firmware, and NAND copies.
- Add session titles and last-session reopening if selected for a future iteration.
- Design long-session context summaries before lifting the current session size limit.
- Verify synthesized audio and sprite-demo controls on a physical DSi.

These items are not completed claims. See `docs/verification.md` for evidence.
