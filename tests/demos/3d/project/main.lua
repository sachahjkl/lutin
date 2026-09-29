local floorMesh, cube, peak, gem
local camX, camZ, mode, frames = 0, -4, 1, 0
local C={0x1d3d49,0x315e6b,0x2c6b70,0xe6b957,0xf07855,0x8bd4b1,0x8b78c8,0xe2ecdf,0x17232f}
local function mesh(v,f) return ds.mesh(v,f) end
local function build_scene()
  local v,f={},{}
  for iz=0,3 do for ix=0,2 do
    local x=-6+ix*4; local z=-2+iz*3.5; local n=#v
    v[n+1]={x,0,z}; v[n+2]={x+4,0,z}; v[n+3]={x+4,0,z+3.5}; v[n+4]={x,0,z+3.5}
    local c=((ix+iz)%2==0) and C[1] or C[2]
    f[#f+1]={n+1,n+2,n+3,c}; f[#f+1]={n+1,n+3,n+4,c}
  end end
  floorMesh=mesh(v,f)
  cube=mesh({{-0.7,0,-0.7},{0.7,0,-0.7},{0.7,1.4,-0.7},{-0.7,1.4,-0.7},{-0.7,0,0.7},{0.7,0,0.7},{0.7,1.4,0.7},{-0.7,1.4,0.7}},
    {{1,2,3,C[4]},{1,3,4,C[4]},{5,7,6,C[5]},{5,8,7,C[5]},{1,5,6,C[2]},{1,6,2,C[2]},{2,6,7,C[6]},{2,7,3,C[6]},{3,7,8,C[3]},{3,8,4,C[3]},{4,8,5,C[7]},{4,5,1,C[7]}})
  peak=mesh({{-0.9,0,-0.8},{0.9,0,-0.8},{0.9,0,0.8},{-0.9,0,0.8},{0,1.8,0}},
    {{1,2,5,C[5]},{2,3,5,C[4]},{3,4,5,C[6]},{4,1,5,C[3]},{1,4,3,C[2]},{1,3,2,C[2]}})
  gem=mesh({{0,0.9,-0.85},{0.8,0.9,0},{0,0.9,0.85},{-0.8,0.9,0},{0,1.9,0},{0,0,0}},
    {{1,2,5,C[6]},{2,3,5,C[4]},{3,4,5,C[3]},{4,1,5,C[5]},{1,6,2,C[2]},{2,6,3,C[1]},{3,6,4,C[2]},{4,6,1,C[1]}})
end
local function frame_update(dt)
  frames=frames+1
  local held,pressed=ds.buttons()
  local speed=0.09
  if held & ds.LEFT~=0 then camX=camX-speed end
  if held & ds.RIGHT~=0 then camX=camX+speed end
  if held & ds.UP~=0 then camZ=camZ+speed end
  if held & ds.DOWN~=0 then camZ=camZ-speed end
  if pressed & ds.A~=0 then mode=mode%3+1 end
  camX=math.max(-4.5,math.min(4.5,camX)); camZ=math.max(-5,math.min(4,camZ))
end
local function render_scene()
  ds.clear(0x101923)
  ds.rect(22,48,2,2,0x4f8290); ds.rect(49,72,1,1,0x526a83)
  ds.rect(78,39,2,2,0x9a7755); ds.rect(105,62,1,1,0x668d96)
  ds.rect(137,47,2,2,0x526a83); ds.rect(171,78,1,1,0x9a7755)
  ds.rect(198,42,2,2,0x4f8290); ds.rect(224,67,1,1,0x668d96)
  ds.rect(244,34,2,2,0x526a83); ds.rect(27,91,1,1,0x9a7755)
  ds.rect(220,93,1,1,0x4f8290); ds.rect(126,86,1,1,0x526a83)
  ds.camera3d(camX,1.55,camZ,0,-0.16)
  ds.draw_mesh(floorMesh,0,0,0,0)
  ds.draw_mesh(cube,-2.3,0,3.5,0); ds.draw_mesh(peak,0,0,5.0,0)
  local signal = mode==1 and gem or (mode==2 and cube or peak)
  ds.draw_mesh(signal,1.7,0,4.2,0)
  ds.draw_mesh(peak,-2.1,0,7.0,0); ds.draw_mesh(cube,3.1,0,7.1,0)
  ds.camera(0,0)
  ds.text(8,7,'SIGNAL GARDEN',0xe2ecdf)
  ds.text(8,20,'LOW-POLY FIELD  /  MODE '..mode,0x8bd4b1)
  ds.text(8,151,'ARROWS: MOVE     A: SHIFT SIGNAL',0xe6b957)
  ds.text(8,164,'CAM '..math.floor(camX*10)/10 .. ', ' .. math.floor(camZ*10)/10,0x9aafbf)
end
function inspect() return {camera={x=camX,z=camZ},mode=mode,frames=frames} end
function init() build_scene() end
function update(dt) frame_update(dt) end
function draw() render_scene() end
