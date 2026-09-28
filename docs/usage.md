# Using Lutin

## Setup

1. Prepare the console with the [installation guide](install-dsi.md).
2. Configure Wi-Fi in the DSi system settings.
3. Set the correct console date and time.
4. Save your OpenCode Go key as plain text in `/lutin/keys/opencode-go`.
5. Keep the supplied CA bundle at `/lutin/ca.pem`.
6. Launch `/roms/nds/lutin.nds` in DSi mode.

The key file contains only the key, without quotes or a `Bearer` prefix.
Lutin validates the server certificate and hostname.
The default model is GPT 6 Luna. START → Model selects a model and stores its name in the session.
Model changes pause the agent and retain Queue.

## Configuration

Lutin reads `/lutin/config.json` at startup:

```json
{
  "default_model": "opencode-go/gpt-6-luna",
  "tool_details": false,
  "startup_view": "keyboard"
}
```

All keys are optional. These are the defaults.
`default_model` uses a `provider/model` identifier.
The bundled IDs are `opencode-go/gpt-6-luna`, `opencode-go/gpt-5.6-luna`, `opencode-go/grok-4.6`, and `opencode-go/deepseek-v4-flash`.
A session's saved model takes priority.
`startup_view` accepts `keyboard` or `sessions`.
`tool_details` expands tool arguments and results in the chat.

Restart Lutin after editing the file. Interface changes do not rewrite this file.
Unknown, duplicate, or invalid keys reject the entire configuration.
Lutin then displays a diagnostic and uses defaults.
The file must contain fewer than 4,096 bytes. Credentials live in `/lutin/keys/<provider-id>`.

## Screens and controls

The upper screen shows the conversation. The lower screen shows the keyboard, preview, game, or selected view.
START opens a modal menu from every view.

| Control                      | Action                                           |
| ---------------------------- | ------------------------------------------------ |
| START                        | Open or close the menu.                          |
| Menu: Up/Down and A          | Select an action. Touch also selects menu items. |
| L / R outside gameplay       | Select Queue / Steer.                            |
| Keyboard: Enter              | Send the draft.                                  |
| Keyboard or preview: Up/Down | Scroll the chat.                                 |
| Keyboard or preview: A       | Return to recent messages.                       |
| Keyboard: B                  | Cancel Queue editing without deleting the draft. |

The menu includes Keyboard, Preview, Play controls, Queue, Source, Sessions, and Model.
It also includes Run program, Stop program, Stop agent, Resume queue, Next project, Save source, Tool details, and Quit.
The keyboard retains the draft when switching views.
Source displays `main.lua`, with Up/Down scrolling.

Only **Play controls** routes input to a creation.
**Preview** displays the creation while keeping its input disabled.
The creation keeps running while the keyboard or menu covers it.
Editing files does not replace the running program. Run program loads the new version explicitly.

## Input priority

The VBlank interrupt samples buttons approximately 60 times per second.
It retains press edges until the main loop consumes them.
An observed press-and-release during a long iteration reaches the next update once through both `pressed` and `held`.
Repeated presses of the same button during that iteration merge into one event.
An input shorter than one sampling interval can still be missed.

The main loop handles input before the runtime and agent.
It defers agent work for an iteration when handling a new press.
The interrupt performs no rendering, storage, network operations, or allocation.
Long native operations can still delay the visible response.

After leaving the menu, only buttons held in the menu remain blocked until released.
Other buttons remain available. START never reaches the creation.
Touch presses retain their initial coordinates for menu selection.
Continuous gameplay touch uses the current position.

## Sessions and projects

START → Sessions lists saved sessions in pages of twelve.
Session count depends on storage, not a fixed set of slots.

| Control         | Action                                 |
| --------------- | -------------------------------------- |
| Up/Down, then A | Open the selected session.             |
| X               | Create a session.                      |
| Right / Left    | Next page / first page.                |
| Y               | Reset the current session, marked `*`. |
| B               | Delete the current session.            |

Switching sessions interrupts the active generation and retains saved results.
Reset clears the conversation, Queue, and tool journal, but retains the selected model.
Reset and delete preserve project files and the running creation.

The interface exposes eight projects under `/lutin/projects/1/` through `/lutin/projects/8/`.
Each project contains Lua files and `session-N.json` journals.
Sessions store the provider-qualified model ID, so names from different providers cannot collide.

## Queue and Steer

Queue saves requests in order. Steer interrupts the current generation and submits a new instruction.
Finished tool effects remain recorded. Interrupted requests are not automatically replayed.

In Queue, Up/Down selects a request:

- A resumes processing after a pause or session reopen.
- X edits the selected request.
- Y moves it to the front.
- Left deletes it.

Editing pauses the agent. Submitting the edit resumes Queue.
Stop agent cancels agent work without stopping the creation.
Stop program ends the creation without clearing the session.

## Network status

WiFi shows OFF before initialization, JOIN during association, ON when connected, or DOWN after disconnection.
Association retries at most three times, with five-second delays.
Idle reconnection attempts run every thirty seconds after a lost connection.
Reconnection does not replay an interrupted AI request.

The status shows DNS, TCP, TLS, Waiting, or Stream with elapsed time and bytes received.
The HTTPS connection timeout is sixty seconds.
A request stops after 180 seconds without data or 600 seconds total.
START → Stop agent cancels the request when control returns to the main loop.

## Creation API

A program defines optional `init()`, `update(dt)`, and `draw()` functions.
`dt` is `1/60`. Actual update frequency depends on runtime and network work.

| Function                              | Result                             |
| ------------------------------------- | ---------------------------------- |
| `ds.clear(color)`                     | Fill the lower-screen framebuffer. |
| `ds.rect(x, y, width, height, color)` | Draw a filled rectangle.           |
| `ds.line(x1, y1, x2, y2, color)`      | Draw a line.                       |
| `ds.text(x, y, text, color)`          | Draw ASCII text.                   |
| `ds.buttons()`                        | Return `held, pressed` masks.      |
| `ds.touch()`                          | Return `x, y, down`.               |

The framebuffer is 256 × 192 pixels. Colors use `0xRRGGBB`.
Coordinates must be integers between −4,096 and 4,096. Drawing clips at screen edges.
Button masks include A = 1, B = 2, Right = 16, Left = 32, Up = 64, and Down = 128.

```lua
local x = 100

function update(dt)
  local held = ds.buttons()
  if held & 16 ~= 0 then
    x = math.min(240, x + 80 * dt)
  end
  if held & 32 ~= 0 then
    x = math.max(0, x - 80 * dt)
  end
end

function draw()
  ds.clear(0x102030)
  ds.rect(math.floor(x), 90, 16, 16, 0x60D0FF)
end
```

## Agent tools

Explicit JSON tools are `read_file`, `write_file`, `patch_file`, `run_program`, `inspect_runtime`, `read_logs`, and `list_files`.
Their schemas describe required arguments.

`read_file(path, offset, limit)` reads up to 4,096 bytes and returns `text`, `version`, `next_offset`, and `total_bytes`.
The version covers the entire file. A missing file returns `text: null` and `version: "missing"`.
`next_offset` is null at the end.

`write_file(path, text, expected_version, append)` accepts up to 8,192 bytes per JSON call.
Use `append: false` to replace or `append: true` to add a chunk.
Writes and patches return the new version. The next operation must use that exact value.
`patch_file` replaces exactly one matching text block. It does not accept unified diffs.

`execute` runs a short Code Mode Lua script in a fresh state with `tools`, `math`, `string`, and `table`.
Lua calls use positional arguments and multiple return values:

| Lua tool                                           | Result                                         |
| -------------------------------------------------- | ---------------------------------------------- |
| `list_files(offset, limit)`                        | Filenames and next offset, at most 32 entries. |
| `read_file(path)`                                  | Text and version, or `nil, "missing"`.         |
| `write_file(path, text, expected_version, append)` | Status and new version. `append` is optional.  |
| `patch_file(path, old, new, expected_version)`     | Status and new version.                        |
| `run_program(path)`                                | Load and start a Lua file.                     |
| `stop_program()`                                   | Stop the creation.                             |
| `inspect_runtime()`                                | Running state, memory, and last error.         |
| `read_logs(cursor)`                                | Diagnostics and next cursor.                   |
| `capture_screen()`                                 | Save `capture.ppm` and return its reference.   |

Paths are flat filenames relative to the active project.
Tools cannot write session journals or access the credential directory.
Captures are not sent to the model as images.

## Limits and recovery

| Resource                             | Limit        |
| ------------------------------------ | ------------ |
| Creation Lua memory                  | 2 MiB        |
| Code Mode Lua memory                 | 512 KiB      |
| Lua instructions per callback        | 100,000      |
| Pixel operations per callback        | 262,144      |
| Tool operations per Code Mode script | 32           |
| Project text file                    | 32 KiB       |
| Tool result                          | 16 KiB       |
| Serialized session                   | 192 KiB      |
| Queued requests                      | 16           |
| Generations per turn                 | 32           |
| Model output budget                  | 4,096 tokens |

Large reads use pages. Large writes use versioned chunks. Oversized tool results request a smaller page or summary.
SSE transport allows 8 MiB without allocating an 8 MiB buffer.
Decoded Chat Completions text, reasoning, and each tool argument buffer are limited to 32 KiB.
At most four calls are accepted per Chat Completions generation.
Output-limit recovery requests a smaller step at most twice per turn. Incomplete calls never execute.
Network loss and timeouts do not trigger this recovery.

Instruction limits do not measure time spent in native code or storage.
Lua pattern functions are removed because their native work bypasses instruction hooks.
`io`, `os`, `package`, `debug`, `load`, `pcall`, and `xpcall` are unavailable to creations.
A syntax error preserves the previous creation. An update or draw error stops the affected creation.

Writes use a temporary file and an internal `.filename.bak` during replacement.
If the main file is missing, a read restores the backup.
Save failures pause the agent and roll back affected Queue changes in memory.
Completed call IDs return recorded results rather than repeating effects.
Interrupted calls remain explicitly uncertain. Partial responses are stored as incomplete.
Physical power-loss recovery on FAT remains a hardware validation task.

## Host live test

```sh
nix build .#live-agent -o result-live-agent
result-live-agent/bin/live-agent /path/to/private-test-directory opencode-go/deepseek-v4-flash 'Create and run an animation.'
```

Place `keys/opencode-go` and `ca.pem` in that directory first.
The model argument uses the same provider-qualified ID as configuration and sessions.
The harness uses the real agent, tools, runtime, and HTTPS modules with host networking.
It writes project files and `result.ppm`. Success requires a completed turn with a running creation.
Keep the test directory outside the repository and Nix store.
