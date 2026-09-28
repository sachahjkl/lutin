local frames = ds.load_asset("hero.lua")
local sounds = ds.load_asset("sounds.lua")
local HERO_WIDTH, HERO_SCALE, HERO_Y = 8, 4, 80
local MOVE_SPEED, ANIMATION_FPS = 80, 6
local MUSIC_VOICE, JUMP_VOICE = 1, 2
local BACKGROUND_COLOR, TITLE_COLOR, HELP_COLOR = 0x102030, 0xffffff, 0x55ddff
local TITLE_X, TITLE_Y, HELP_X, HELP_Y = 24, 24, 16, 144
local MAX_X = ds.WIDTH - HERO_WIDTH * HERO_SCALE
local x, time, flip = MAX_X / 2, 0, false

function init()
  ds.play_sound(sounds.music, MUSIC_VOICE, true)
end

function update(dt)
  local held, pressed = ds.buttons()
  time = time + dt
  if held & ds.LEFT ~= 0 then
    x, flip = math.max(0, x - MOVE_SPEED * dt), true
  end
  if held & ds.RIGHT ~= 0 then
    x, flip = math.min(MAX_X, x + MOVE_SPEED * dt), false
  end
  if pressed & ds.A ~= 0 then
    ds.play_sound(sounds.jump, JUMP_VOICE)
  end
  if pressed & ds.B ~= 0 then
    ds.play_sound(sounds.hit, ds.NOISE_VOICE)
  end
end

function draw()
  ds.clear(BACKGROUND_COLOR)
  ds.text(TITLE_X, TITLE_Y, "LUTIN SPRITES + AUDIO", TITLE_COLOR)
  ds.draw_sprite(frames[math.floor(time * ANIMATION_FPS) % #frames + 1], math.floor(x), HERO_Y, HERO_SCALE, flip)
  ds.text(HELP_X, HELP_Y, "D-PAD MOVE  A TONE  B NOISE", HELP_COLOR)
end
