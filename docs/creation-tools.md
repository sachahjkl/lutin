# Creation tools

## Execution and inspection

`start_test(path, seed)` starts a creation with a repeatable random seed and pauses automatic frame updates.
`step_frames(frames, buttons, touch_x, touch_y)` advances up to 120 fixed-time frames per tool execution.
Button edges derive from the previous test input. Include the TOUCH flag to hold the touchscreen.
`finish_test()` returns control to live input.
`inspect_runtime()` reports runtime metrics and the JSON-compatible value returned by the creation's optional `inspect()` function.
Inspection has the same instruction limit as other callbacks and a separate bounded serialization limit.

```lua
tools.start_test("main.lua", 17)
local before = tools.inspect_runtime().state
local after = tools.step_frames(60, tools.RIGHT, 0, 0).state
assert(after.player.x > before.player.x)
tools.step_frames(1, tools.RIGHT | tools.A, 0, 0)
local jumping = tools.step_frames(8, tools.RIGHT, 0, 0).state
assert(not jumping.player.grounded)
return { before = before, after = after, jumping = jumping }
```

Use another call to `finish_test()` when testing finishes.
The interface shows `TEST` while automatic updates are paused.
The console yields between test frames so network threads can run.
The program's `inspect()` callback returns a table, for example:

```lua
function inspect()
    return { player = { x = player.x, y = player.y, grounded = player.grounded }, score = score }
end
```

Inspection supports finite numbers, strings, booleans, arrays, and string-keyed tables.
It rejects cycles, unsupported types, depth above eight, more than 256 nodes, or more than 8 KiB of string/key data.
An inspection failure appears as `inspection_error`; it does not stop a running creation.
Code Mode results allow depth twelve, 1,024 nodes, and 12 KiB of string/key data to accommodate diagnostic fields around valid state.
Frame metrics include callback and drawing work. They do not measure a guaranteed display refresh rate.

## Visual feedback

Captures use PNG and carry immutable project-local references in session history.
Only models marked with image-input support receive image content in requests.
Responses and Chat Completions each encode the image in their native request format.
Text-only models receive an explicit unsupported result rather than a claim of visual inspection.
The catalog's `image_input` flag comes from models.dev input modalities and can be set in personal model entries.
One latest capture is attached per request to bound request memory.
Session journals retain the capture filename, not base64 image data.
Captures use `capture-N.png`; later captures do not overwrite earlier ones.

## Reusable APIs

Native helpers cover tilemap drawing, camera offsets, rectangle collision, animation frame selection, text measurement, and touch buttons.
Project modules load during startup and cache their return values.
Save data uses JSON through project-local storage callbacks, separate from program source.

| API                                      | Behavior                                                                                                             |
| ---------------------------------------- | -------------------------------------------------------------------------------------------------------------------- |
| `ds.camera(x, y)`                        | Offset 2D world drawing. Use `(0, 0)` for a fixed HUD.                                                               |
| `ds.tilemap(rows, sprites, tile_size)`   | Compile a map of up to 64 × 64 tiles. `.` is empty; `0`–`f` select sprites.                                          |
| `ds.draw_tilemap(map, x, y)`             | Draw visible tiles. Each sprite must match the tile size.                                                            |
| `ds.overlap(x, y, w, h, x2, y2, w2, h2)` | Test rectangle overlap. Touching edges do not overlap.                                                               |
| `ds.animation(frames, ticks_per_image)`  | Create a looping animation of 1–128 sprites and 1–60 ticks per image.                                                |
| `ds.draw_animation(animation, x, y)`     | Select a frame from the creation's frame counter and draw it.                                                        |
| `ds.measure_text(text)`                  | Return width and height in pixels, including newlines and supported UTF-8 glyphs.                                    |
| `ds.button(x, y, w, h, label)`           | Draw a screen-space button. Return true on a touch press inside it.                                                  |
| `ds.module(path)`                        | Load and cache a project Lua module. Modules must return a value. Cycles fail.                                       |
| `ds.save(value)`                         | Replace `save-data.json` with at most 8 KiB of JSON. Available during live callbacks, not test execution or startup. |
| `ds.load_save()`                         | Read project save data, or return nil when unavailable.                                                              |

Compile assets and load modules at startup or in `init()`.
Save-data access permits at most four operations and 16 KiB per callback, including inspection.
Each read reserves 8 KiB of that budget before accessing storage. Writes charge their serialized size.
The font covers ASCII, Latin-1 accented letters, and `Œ`/`œ`.
Common combining accents are composed during decoding. Unsupported characters render as `?` instead of disappearing.

## Software 3D

`ds.mesh(vertices, faces)` compiles up to 256 vertices and 256 triangles.
Each vertex is `{x, y, z}`. Each face is `{first, second, third, 0xRRGGBB}`, with one-based indices.
`ds.draw_mesh(mesh, x, y, z, yaw)` places and rotates a mesh about its Y axis.
`ds.camera3d(x, y, z, yaw, pitch)` sets the view; angles are radians.
Positive Z points forward. Positive Y points upward.
The renderer clips against a 0.25-unit near plane and uses a 160-pixel focal length.
Triangles have flat colors and depth testing. There are no textures or lighting APIs in this version.

This is a native CPU renderer into the existing framebuffer, not the DSi hardware 3D engine.
It shares the creation drawing budget. Keep scenes small and inspect frame timings.
Its fixed scratch area holds transformed vertices and the depth buffer without per-frame allocation.

## Checkpoints

Checkpoints contain project source and assets, excluding sessions, captures, and save data.
Restore validates the complete checkpoint before changing project files.
An interrupted restore remains recoverable and must finish before another project edit.
`create_checkpoint(name)` replaces a named checkpoint; `restore_checkpoint(name)` stops the creation and restores its files.
Names contain 1–24 letters, digits, dashes, or underscores.
Each checkpoint holds at most 64 source files and 512 KiB of serialized data.
Restore removes source files that were added after the checkpoint.
After restoration, read file versions again before editing and explicitly run the program.

## Budgets

Memory and instruction caps remain explicit policies, not hardware requirements.
Test execution, serialization, assets, and checkpoints each have bounded work and storage.
Native-operation durations remain hardware measurements, not instruction-limit guarantees.
The 2 MiB creation and 512 KiB Code Mode caps apply to their Lua allocators, not total process memory.
Framebuffers, renderer scratch, PNG compression, transport, and checkpoint serialization also use native memory.
