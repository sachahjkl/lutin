local frames = ds.load_asset("hero.lua")
local sounds = ds.load_asset("sounds.lua")
local x, time, flip = 112, 0, false

function init()
  ds.play_sound(sounds.music, 1, true)
end

function update(dt)
  local held, pressed = ds.buttons()
  time = time + dt
  if held & 32 ~= 0 then
    x, flip = math.max(0, x - 80 * dt), true
  end
  if held & 16 ~= 0 then
    x, flip = math.min(224, x + 80 * dt), false
  end
  if pressed & 1 ~= 0 then
    ds.play_sound(sounds.jump, 2)
  end
  if pressed & 2 ~= 0 then
    ds.play_sound(sounds.hit, 4)
  end
end

function draw()
  ds.clear(0x102030)
  ds.text(24, 24, "LUTIN SPRITES + AUDIO", 0xffffff)
  ds.draw_sprite(frames[math.floor(time * 6) % #frames + 1], math.floor(x), 80, 4, flip)
  ds.text(16, 144, "D-PAD MOVE  A TONE  B NOISE", 0x55ddff)
end
