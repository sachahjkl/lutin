# Agent interface

## Screens

The upper screen displays colored chat and status.
The lower screen displays the keyboard by default, or a menu-selected preview, game, or list.
START opens the menu in every view.
The status identifies the project, session, Wi-Fi state, and agent progress.
Tool activity includes inspectable arguments and results.
Each screen is 256 × 192 pixels.

## Requests

- Queue adds a request for processing after the active turn.
- Steer interrupts the active generation and submits a priority instruction.
- Queue editing pauses the agent and resumes after submission.
- Stop agent cancels agent work without stopping the creation.
- Stop program stops the creation without deleting the session.

Completed file effects remain recorded after interruption.
An in-progress native operation finishes before the main loop can process cancellation.

## Input routing

Only Play controls supplies buttons and touch to the program.
Preview shows the framebuffer with game input disabled.
The keyboard retains the draft across view switches.
VBlank sampling retains short physical-button presses across expensive main-loop work.
Menu-held keys remain blocked individually until released.
START is reserved for the host.

## Sessions

Sessions are created on demand and listed in pages of twelve.
The current session is marked `*`.
Reset clears history, Queue, and tool journal while retaining the model.
Delete removes the current session. Neither action removes project creations.
Changing sessions pauses the agent. Selecting an invalid session preserves the previous session.

## Verification

Test input during requests, return from gameplay, draft retention, Queue, Steer, and independent stop actions.
Test a short press during a stalled consumer and a new button while a menu-held direction remains down.
