# Hardware rendering

## Design

The console renderer submits bounded display lists to the DS geometry engine.
The host renderer remains a reference implementation for logic and sanitizer tests.
Console tests must inspect hardware captures; host rendering does not prove hardware output.

The GPU transforms and rasterizes meshes, rectangles, lines, sprites, tilemaps, and text.
CPU plane tests select boundary triangles for near/far clipping before submission.
Sprite textures live in a program-owned VRAM arena. The font uses one shared texture atlas.
Audio already uses hardware square-wave and noise channels.

VRAM ownership:

| Bank | Owner                             |
| ---- | --------------------------------- |
| A, B | Texture allocator, 256 KiB total  |
| C    | Upper-screen text engine          |
| D    | Display capture, LCD mapping      |
| E    | Lower-screen console and keyboard |

The creation uses main-engine BG0 (3D). The lower console uses BG2.
The keyboard uses BG1. The main engine remains on the touch screen.
Captures select the 3D output directly, excluding console and keyboard layers.

## Limits and ordering

Each frame has at most 2,048 polygons and 6,144 reserved vertices.
Each polygon reserves six extra vertices for hardware clipping, in addition to submitted vertices.
Each 2D phase permits 512 primitives. Text glyphs count as primitives.
Background 2D draws precede meshes. Subsequent 2D draws form the HUD above the mesh scene.
Mesh depth occupies a separate range from these two phases.
Near/far clipping occurs at 0.25 and 128 camera units before GPU submission.
The GPU performs projection, viewport clipping, rasterization, and depth testing.

Each program owns at most 96 KiB of sprite textures and 128 texture handles.
This leaves space for the font and a candidate program during transactional startup.
Texture allocation failures reject startup or drawing without a software fallback.
Frame commands are copied into a fixed arena before submission; they contain no Lua pointers.
Failed callbacks discard unsubmitted commands.

Draw complete frames. A frame with no draw commands retains the last submitted image.
`ds.clear()` discards earlier commands in the current frame and sets its background color.

## Verification

Test hardware captures for sprite transparency, font glyphs, layering, mesh depth, clipping, and view switches.
Exercise seeded stepping and image requests with the hardware renderer.
Require 2D/3D emulator gameplay acknowledgements and negative freeze/inspection checks.
Report CPU submission timing separately from complete main-loop and presentation timing.
Physical DSi timing remains a separate measurement.

## Other work reductions

The display loop transfers console maps only after their contents change.
It does not copy a RAM creation framebuffer every frame.
Chat pages are cached by conversation revision, scroll position, and tool-detail mode.
Streaming text and tool arguments track their used lengths instead of rescanning accumulated buffers for each delta.
Inspection exposes main-loop work, UI work, agent work, display bytes, and skipped VBlanks separately.
