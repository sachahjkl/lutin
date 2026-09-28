# Specification: Lutin

**Feature:** `001-dsi-ai-sandbox`
**Created:** 2026-09-27
**Status:** implemented coding agent with continuing hardware validation.

## Intent

Build a DSi application connected to OpenCode Go.
The user describes creations on the console. An autonomous agent writes, runs, and refines interactive Lua programs.
The sandbox is general-purpose. Games are examples rather than a fixed product format.

## User stories

### US1 — Talk to the AI from the DSi — P1

The user types a request and reads the response on the console.
The application identifies authentication and connection errors and supports cancellation.
Verification: a short authenticated request produces a visible response on a physical DSi.

### US2 — Run a local creation — P1

The user runs a saved or bundled program without an AI connection.
The creation draws to the lower screen and reads buttons or touch in Play controls.
Errors preserve access to the interface.
Verification: run an offline example, trigger a Lua failure, and start a valid program afterward.

### US3 — Create and modify with AI — P2

The user requests a creation and then asks for a visible change.
The agent receives only the implemented sandbox API and tools.
Verification: two successive program versions produce different visible behavior without leaving the console.

### US4 — Resume agent work — P1

The user opens a saved session and continues a project.
The journal retains messages, tool calls, and results.
Verification: resume a saved session without replaying completed actions or losing Queue.

## Functional requirements

- **FR-001:** boot from the selected homebrew launcher in DSi mode.
- **FR-002:** distinguish Wi-Fi, DNS, TLS, HTTP, and model failures.
- **FR-003:** preserve the draft when submission fails.
- **FR-004:** decode streaming responses with bounded memory.
- **FR-005:** run creations locally with documented budgets.
- **FR-006:** provide a stop command during execution.
- **FR-007:** save and reload creations on SD.
- **FR-008:** run saved creations offline.
- **FR-009:** load the OpenCode Go key from an SD file at runtime.
- **FR-010:** expose sessions, project files, agent activity, and graphical results.
- **FR-011:** provide file, execution, and diagnostic tools with explicit contracts.
- **FR-012:** persist each tool-call ID and result.
- **FR-013:** retain essential input while rendering or networking is busy.
- **FR-014:** bound model context and stored history. The initial implementation uses size limits, not automatic summaries.
- **FR-015:** restore completed actions without executing them twice.
- **FR-016:** support multiple persistent sessions per project.
- **FR-017:** allow only one active agent to modify a project.
- **FR-018:** process Steer before pending Queue requests at the next interruption point.
- **FR-019:** preserve Queue order and allow editing or deletion before execution.
- **FR-020:** stop the agent independently of the creation.
- **FR-021:** support local grouped tools through bounded Code Mode scripts.
- **FR-022:** create, select, reset, and delete sessions without predefined session slots.
- **FR-023:** load optional configuration with strict validation and visible errors.
- **FR-024:** use public service endpoints without personal infrastructure dependencies.
- **FR-025:** provide English documentation, an MIT license, and reproducible release artifacts.

## Edge cases

- DS-mode launch instead of DSi mode.
- Incorrect console clock or failed certificate validation.
- A stream interrupted inside an event or UTF-8 sequence.
- Oversized output, malformed arguments, invented versions, or missing APIs.
- Lua memory exhaustion, infinite loops, and native callback failures.
- Missing SD card, full storage, blocked temporary paths, and failed replacement.
- A short button press during a long main-loop iteration.
- A held direction while closing the menu and pressing another game button.

## Success criteria

- **SC-001:** a clean checkout builds a ROM through the documented command.
- **SC-002:** a physical DSi sends a request and displays an OpenCode Go response.
- **SC-003:** the user creates and modifies a visible example on the console.
- **SC-004:** an infinite loop stops without restarting the console. A sub-second hardware target remains to be measured.
- **SC-005:** a saved creation runs offline after reboot.
- **SC-006:** a resumed session does not replay completed tool calls.
- **SC-007:** START returns from gameplay to the menu.
- **SC-008:** a sampled press-and-release during a consumer stall is delivered once afterward.

See `docs/verification.md` for observed results and remaining hardware measurements.
