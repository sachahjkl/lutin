# Sprites and audio

These APIs are available from `v0.4.0`.
The coding agent generates assets through the existing versioned file tools.
Assets are editable Lua text, stored beside `main.lua` in the project.
The same model creates pixel art, animation frames, melodies, and sound effects.

## Load assets

Call `ds.load_asset("hero.lua")` at startup or in `init()`.
The asset executes in the program sandbox and returns a value.
Asset code has the same instruction and memory limits as the program.
Paths use the project filename restrictions. Binary Lua and external paths are rejected.
Each file is limited to 32 KiB. Startup and `init()` each have a 64 KiB asset-read budget.
Load assets once. Asset loading during `update()` or `draw()` fails.

## Pixel sprites

Save this as `hero.lua`:

```lua
return ds.sprite({
  ".00.",
  "0110",
  "0220",
  ".00.",
}, {0x55ddff, 0xffffff, 0x19334d})
```

Rows have equal widths. Width and height are each 1–64 pixels.
A dot is transparent. Symbols `0123456789abcdef` select palette entries 1–16.
Colors use `0xRRGGBB`.
The runtime compiles rows into a memory-accounted sprite handle.

```lua
local hero = ds.load_asset("hero.lua")
function draw()
  ds.clear(0x102030)
  ds.draw_sprite(hero, 100, 80, 4, false)
end
```

Scale defaults to 1 and accepts integers 1–8. `flip` defaults to false and mirrors horizontally.
Drawing clips at screen edges and uses the existing pixel budget.
Sprites use the framebuffer renderer.
For animation, return a table of sprites and select a frame in `draw()`.
See [`examples/hero.lua`](../examples/hero.lua) for a two-frame example.

## Synthesized audio

Save this as `jump.lua`:

```lua
return ds.sound("square", {
  {440, 4, 80},
  {660, 4, 70},
  {880, 6, 50},
})
```

Each note contains `{frequencyHz, durationFrames, volume}`:

- Frequency is 32–16,000 Hz. Zero creates a rest.
- Duration is 1–3,600 runtime frames.
- Volume is 0–127.
- A sequence contains 1–128 notes.

```lua
local jump = ds.load_asset("jump.lua")
function update(dt)
  local held, pressed = ds.buttons()
  if pressed & ds.A ~= 0 then
    ds.play_sound(jump, 2)
  end
end
```

Square waves use voices 1–3. Voice 1 is the default.
Noise uses `ds.NOISE_VOICE`. Create noise sequences with `ds.sound("noise", notes)`.
The runtime uses libnds PSG and noise playback.
Use `ds.play_sound(music, 1, true)` for a looping melody.
Use another voice for effects during music playback.
Use `ds.stop_sound(voice)` to stop a voice.
Replacing a voice releases its previous sequence reference.
Stopping or failing the program stops all voices.
A rejected replacement program leaves the current program and audio running.

Sequencing advances with runtime frames, nominally 60 per second.
Long native operations can extend a note until the next runtime frame.
Physical-console audio output still needs listening verification.

## Bundled example

The source-built SD kit includes an animated sprite, looping music, and two effects.
Open Play controls. Move with the D-pad. Press A for a tone effect. Press B for noise.

The source files are [`media.lua`](../examples/media.lua), [`hero.lua`](../examples/hero.lua), and [`sounds.lua`](../examples/sounds.lua).
Ask the agent to edit these files or create new assets, then run the program and inspect runtime errors.
