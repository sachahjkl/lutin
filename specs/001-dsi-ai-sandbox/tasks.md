# Implementation record

Backlog tools were unavailable. This Spec Kit record does not modify `BACKLOG.json`.

## Foundation

- [x] T001 Initialize Git and install Spec Kit.
- [x] T002 Add Nix, BlocksDS, and flake checks.
- [x] T003 Build the native boot ROM.
- [x] T004 Document the inference service and provide the live-agent harness.
- [x] T005 Record checks and evidence in `docs/verification.md`.
- [x] T006 Record product decisions in `grill-me.md`.
- [x] T007 Define views and controls.
- [x] T008 Define agent tools.

## Network and dialogue

- [x] T009 Obtain user confirmation of DSi-mode networking and inference.
- [x] T010 Validate authenticated inference and tools on the host.
- [x] T011 Add bounded SSE parsing and fragmentation tests.
- [x] T012 Add English input, cancellation, and display.

## Sessions and agent

- [x] T013 Add persistent journals and resume behavior.
- [x] T014 Test interruptions and completed-call non-replay.
- [x] T015 Add bounded requests and the autonomous tool loop.
- [x] T022 Add Steer, Queue, ordering, and interruption tests.
- [x] T028 Add dynamic sessions, reset, delete, pagination, and failed-switch recovery.
- [x] T029 Add strict SD configuration.

## Runtime and storage

- [x] T016 Select Lua and define its contract.
- [x] T017 Add drawing, input, and execution budgets.
- [ ] T018 Measure infinite-loop recovery and memory exhaustion on physical hardware.
- [x] T019 Connect file and runtime tools to the agent.
- [x] T020 Add FAT-safe replacement and interrupted-call reporting.
- [x] T021 Verify creation, modification, and offline persistence on the host.
- [ ] T027 Test power loss during FAT writes on console.

## Installation and emulation

- [x] T023 Add melonDS checks to the flake.
- [x] T024 Pin and verify installation downloads.
- [x] T025 Document console installation.
- [ ] T026 Validate full DSi emulation with console-owned dumps.
- [x] T030 Add VBlank input capture and host/emulator regressions.
- [ ] T031 Validate input behavior during live inference on physical hardware.

## Publication

- [x] T032 Remove the personal inference endpoint and document OpenCode Go.
- [x] T033 Add the supplied logo, project metadata, MIT license, and English documentation.
- [ ] T034 Publish the GitHub repository, CI, and release artifacts after final checks.
