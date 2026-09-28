local x, y = 30, 40
local vx, vy = 75, 55
local width, height = 32, 22

function init()
  x, y = 30, 40
  vx, vy = 75, 55
end

function update(dt)
  x = x + vx * dt
  y = y + vy * dt
  if x < 0 then
    x = 0
    vx = math.abs(vx)
  elseif x > 256 - width then
    x = 256 - width
    vx = -math.abs(vx)
  end
  if y < 0 then
    y = 0
    vy = math.abs(vy)
  elseif y > 192 - height then
    y = 192 - height
    vy = -math.abs(vy)
  end
end

function draw()
  ds.clear(0x101820)
  ds.rect(math.floor(x), math.floor(y), width, height, 0x00FFFF)
end
