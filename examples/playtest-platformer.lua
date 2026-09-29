-- Lantern Run: a fixed-step rooftop platformer with one-way platforms.
local player, frame, score, scene, camera_x
local terrain, hero, lantern, stars, lights
local platforms = {
  { x = 0, y = 160, width = 512 },
  { x = 80, y = 128, width = 48 },
  { x = 176, y = 104, width = 48 },
  { x = 272, y = 128, width = 48 },
  { x = 368, y = 104, width = 48 },
}
local floor = math.floor

function init()
  frame, score, scene, camera_x = 0, 0, "rooftops", 0
  player = { x = 24, y = 146, velocity_y = 0, grounded = true, left = false }
  lights = {
    { x = 56, y = 146 },
    { x = 104, y = 114 },
    { x = 200, y = 90 },
    { x = 296, y = 114 },
    { x = 392, y = 90 },
  }
  stars = {}
  for index = 1, 24 do
    stars[index] = { x = math.random(0, 255), y = math.random(36, 116) }
  end
  hero = ds.sprite({
    "...0000...",
    "..000000..",
    "..122221..",
    "..122321..",
    "...2222...",
    "..444444..",
    ".444444440",
    "0444444440",
    "00.4444.00",
    "...4444...",
    "...5555...",
    "...5..5...",
    "..55..55..",
    "..00..00..",
  }, { 0x14283f, 0xf5b45b, 0xffe1ad, 0x14283f, 0x67e8ce, 0x31577a })
  lantern = ds.sprite({
    "..00..",
    ".0110.",
    "012210",
    "012210",
    "012210",
    ".0110.",
    "..00..",
  }, { 0xd98948, 0xffcd68, 0xfff2c4 })
  local roof = ds.sprite({
    "00000000",
    "11111111",
    "22222222",
    "22322223",
    "22222222",
    "33333333",
    "23222222",
    "22222222",
  }, { 0x78d9cc, 0x3d969c, 0x304763, 0x26384f })
  local rows = {}
  for row = 0, 21 do
    local cells = {}
    for column = 0, 63 do
      local solid = row >= 20
      for _, platform in ipairs(platforms) do
        if row * 8 == platform.y and column * 8 >= platform.x and column * 8 < platform.x + platform.width then
          solid = true
        end
      end
      cells[column + 1] = solid and "0" or "."
    end
    rows[row + 1] = table.concat(cells)
  end
  terrain = ds.tilemap(rows, { roof }, 8)
end

function update()
  frame = frame + 1
  local held, pressed = ds.buttons()
  if scene == "complete" then
    if pressed & ds.A ~= 0 then
      init()
    end
    return
  end
  local direction = 0
  if held & ds.RIGHT ~= 0 then
    direction = direction + 1
  end
  if held & ds.LEFT ~= 0 then
    direction = direction - 1
  end
  if direction ~= 0 then
    player.left = direction < 0
  end
  player.x = math.max(0, math.min(502, player.x + direction * 2))
  if pressed & ds.A ~= 0 and player.grounded then
    player.velocity_y = -6
    player.grounded = false
  end
  local previous_bottom = player.y + 14
  player.velocity_y = math.min(6, player.velocity_y + 0.3)
  player.y = player.y + player.velocity_y
  player.grounded = false
  for _, platform in ipairs(platforms) do
    if
      player.velocity_y >= 0
      and previous_bottom <= platform.y
      and player.y + 14 >= platform.y
      and player.x + 10 > platform.x
      and player.x < platform.x + platform.width
    then
      player.y = platform.y - 14
      player.velocity_y = 0
      player.grounded = true
    end
  end
  for _, light in ipairs(lights) do
    if not light.collected and ds.overlap(player.x, floor(player.y), 10, 14, light.x, light.y, 6, 7) then
      light.collected = true
      score = score + 1
    end
  end
  if player.x >= 472 then
    scene = "complete"
  end
  camera_x = math.max(0, math.min(256, player.x - 96))
end

local function centered(y, text, color)
  local width = ds.measure_text(text)
  ds.text(floor((256 - width) / 2), y, text, color)
end

function draw()
  ds.camera(0, 0)
  ds.clear(0x14243d)
  for index, star in ipairs(stars) do
    local bright = (floor(frame / 30) + index) % 4 == 0
    ds.rect((star.x - floor(camera_x / 4)) % 256, star.y, 1, 1, bright and 0xffe1ad or 0x64819a)
  end
  ds.rect(211, 46, 17, 17, 0xffdf9c)
  ds.rect(207, 43, 15, 15, 0x14243d)
  for index = 0, 9 do
    local x = index * 32 - floor(camera_x / 3) % 32
    local y = 104 + (index % 3) * 12
    ds.rect(x, y, 26, 56, 0x20344f)
    ds.rect(x + 6, y + 8, 3, 5, 0x65717b)
    ds.rect(x + 16, y + 20, 3, 5, 0x65717b)
  end
  ds.camera(camera_x, 0)
  ds.draw_tilemap(terrain, 0, 0)
  for _, light in ipairs(lights) do
    if not light.collected then
      local bob = floor(frame / 16) % 2
      ds.rect(light.x - 2, light.y - 2 - bob, 10, 11, 0x514738)
      ds.draw_sprite(lantern, light.x, light.y - bob)
    end
  end
  ds.line(480, 118, 480, 159, 0xffdf9c)
  ds.rect(481, 119, 17, 10, 0x67e8ce)
  ds.rect(481, 129, 10, 3, 0x3d969c)
  ds.draw_sprite(hero, player.x, floor(player.y), 1, player.left)
  if player.grounded and frame % 12 < 6 then
    ds.rect(player.x + 2, floor(player.y) + 13, 2, 1, 0x67e8ce)
  end
  ds.camera(0, 0)
  ds.rect(0, 0, 256, 30, 0x0b182b)
  ds.text(8, 6, "LANTERN RUN", 0xffdf9c)
  ds.text(160, 6, "LIGHTS " .. score .. "/5", 0x67e8ce)
  ds.rect(8, 21, 240, 2, 0x304763)
  ds.rect(8, 21, floor(player.x * 240 / 502), 2, 0x67e8ce)
  ds.rect(0, 176, 256, 16, 0x0b182b)
  centered(180, "D-PAD MOVE  A JUMP", 0xc5d9e6)
  if scene == "complete" then
    ds.rect(23, 59, 210, 66, 0x67e8ce)
    ds.rect(25, 61, 206, 62, 0x0b182b)
    centered(70, "DELIVERY COMPLETE", 0xffdf9c)
    centered(88, "LIGHTS " .. score .. " / 5", 0x67e8ce)
    centered(106, "A: RUN AGAIN", 0xc5d9e6)
  end
end

function inspect()
  return {
    player = { x = player.x, y = player.y, grounded = player.grounded },
    score = score,
    frame = frame,
    scene = scene,
  }
end
