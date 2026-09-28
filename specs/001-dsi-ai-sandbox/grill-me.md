# Product interview decisions

**Status:** closed at the user's request. Routine technical decisions are delegated to the development agent.

## Confirmed direction

The product is a general-purpose sandbox with an autonomous programming agent.
Games, graphics, animations, and touch interaction are part of the goal.
A breakout game is a validation example, not a restriction on supported creations.
The interface is development-oriented, with persistent AI sessions and visible tool activity.

The initial hardware is a DSi XL.
The user accepted a console-SD installation of Unlaunch and TWiLight Menu++ to access DSi-mode capabilities.
An R4 was available, but it is not the validated launch path.

## Agent workflow

The agent writes, runs, and fixes programs without confirmation at every tool call.
The user retains a stop command and can inspect actions.
Short Code Mode scripts can group tool operations.
Lua 5.4 uses separate states for tools and creations.

Steer redirects current work at the next available interruption point.
Queue stores subsequent requests in order and supports editing or deletion before execution.
One active agent owns the project. Multiple sessions preserve separate conversations.
Stopping the agent is separate from stopping the creation.

## Interface

The user selected an OpenCode-style terminal layout.
Chat occupies the upper screen. Keyboard, preview, and gameplay share the lower screen through a START menu.
The draft survives view switches.
The user requested Wi-Fi state, elapsed request time, received bytes, and model selection.

## Later decisions

- Use the name **Lutin**.
- Support a configuration file on SD.
- Create sessions on demand, with switch, reset, and delete actions.
- Preserve short button presses while the main loop is busy.
- Remove the personal inference endpoint and use OpenCode Go directly.
- Publish under MIT with English documentation, a presentation video, and a Mermaid architecture diagram.
- Store the supplied logo and metadata under `.project/`.

## Verification expectations

Provide emulator checks, reproducible builds, and installation instructions.
Separate physical-console feedback from host and emulator evidence.
Measure hardware latency, memory, and power-loss behavior before claiming those properties.
