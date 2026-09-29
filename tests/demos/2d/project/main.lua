local W,H=256,192
local px,py,vx,vy=24,144,0,0
local pw,ph=10,16
local grounded=false
local score,frames=0,0
local cam=0
local speed,gravity,jump=2.15,0.28,-5.4
local platforms={{0,160,164},{205,145,105},{348,128,125},{510,150,130},{675,132,155},{865,155,155},{1055,137,190}}
local lights={{76,143},{126,136},{239,128},{289,119},{382,102},{447,103},{551,124},{613,124},{713,106},{782,106},{900,129},{981,129},{1102,111},{1180,111}}
local got={}
local stars={{9,18},{35,44},{58,12},{82,34},{109,20},{139,50},{164,15},{188,31},{218,11},{243,46},{22,76},{71,65},{124,78},{173,70},{226,83}}
local hills={{0,152},{35,126},{71,151},{112,117},{153,150},{197,123},{237,150},{280,118},{325,151}}
local playerArt
function init()
 playerArt=ds.sprite({'...11...','...11...','...22...','...22...','..2222..','..2332..','..2332..','...22...','..2442..','..2442..','...44...','...44...','..4..4..','..4..4..','.44..44.','.44..44.'}, {0x000000,0xf6c75b,0xffe8aa,0x79a9bc,0x42627b})
end
local function overlapLight(x,y,lx,ly) return ds.overlap(math.floor(x),math.floor(y),pw,ph,lx-4,ly-5,8,10) end
function update(dt)
 frames=frames+1
 local held,pressed=ds.buttons()
 vx=0
 if held & ds.LEFT~=0 then vx=-speed end
 if held & ds.RIGHT~=0 then vx=speed end
 if pressed & ds.A~=0 and grounded then vy=jump; grounded=false end
 local oldY=py
 px=math.max(0,math.min(1230,px+vx))
 py=py+vy
 vy=math.min(6,vy+gravity)
 grounded=false
 if vy>=0 then
  for _,p in ipairs(platforms) do
   if oldY+ph<=p[2] and py+ph>=p[2] and px+pw>p[1] and px<p[1]+p[3] then py=p[2]-ph; vy=0; grounded=true end
  end
 end
 if py>190 then py=144; vy=0 end
 for i,l in ipairs(lights) do if not got[i] and overlapLight(px,py,l[1],l[2]) then got[i]=true; score=score+1 end end
 cam=math.max(0,math.min(1230-W,px-92))
end
local function box(x,y,w,h,c) ds.rect(math.floor(x),math.floor(y),w,h,c) end
function draw()
 ds.clear(0x080f24)
 -- distant stars and layered night hills
 for i,s in ipairs(stars) do local sx=(s[1]-cam*0.12)%256; box(sx,s[2],(i%4==0) and 2 or 1,1,(i%3==0) and 0x8296b8 or 0x526487) end
 box(225,42,11,11,0xc2cee0); box(229,39,11,11,0x080f24)
 for i=1,#hills-1 do
  local x1,y1=hills[i][1]-cam*0.27,hills[i][2]; local x2,y2=hills[i+1][1]-cam*0.27,hills[i+1][2]
  ds.line(math.floor(x1),y1,math.floor(x2),y2,0x18233b)
  ds.line(math.floor(x1),y1+7,math.floor(x2),y2+7,0x111b31)
 end
 -- low horizon silhouettes
 for i=1,8 do local x=((i*47-cam*0.48)%330)-40; box(x,145-(i%3)*5,2,14+(i%3)*5,0x101a2d); box(x-3,148-(i%3)*5,8,2,0x101a2d) end
 for _,p in ipairs(platforms) do
  local x=math.floor(p[1]-cam)
  if x<256 and x+p[3]>0 then
   box(x,p[2],p[3],H-p[2],0x26344c); box(x,p[2],p[3],3,0x59708b)
   box(x+4,p[2]+7,math.max(0,p[3]-8),1,0x34445d)
  end
 end
 for i,l in ipairs(lights) do if not got[i] then
  local x=math.floor(l[1]-cam); local y=l[2]
  box(x-4,y-3,8,8,0x513e37); box(x-2,y-5,4,10,0xf0b94e); box(x-1,y-3,2,6,0xffedaa); box(x-1,y+6,2,2,0xd68a3d)
 end end
 local sx,sy=math.floor(px-cam),math.floor(py)
 ds.draw_sprite(playerArt,sx,sy,1,false)
 -- high-contrast silhouette stays legible against the night platforms
 box(sx,sy,10,16,0x111b31); box(sx+2,sy,6,5,0xf6c75b)
 box(sx+4,sy+2,2,2,0x18243a); box(sx+1,sy+5,8,2,0xffd36a)
 box(sx+1,sy+7,8,5,0x39a9bd); box(sx,sy+7,2,4,0xf0b94e); box(sx+8,sy+7,2,4,0xf0b94e)
 box(sx+2,sy+12,2,2,0xd4deef); box(sx+6,sy+12,2,2,0xd4deef)
 box(sx+1,sy+14,4,2,0x26344c); box(sx+6,sy+14,4,2,0x26344c)
 -- HUD
 box(6,6,102,25,0x101a30); box(6,6,2,25,0xe8b34d)
 ds.text(13,8,'LANTERN RUN',0xffd36a); ds.text(13,19,'LIGHTS '..score..' / '..#lights,0xd4deef)
 ds.text(158,8,'LEFT / RIGHT',0xb9c7df); ds.text(199,19,'A  JUMP',0xffd36a)
 box(0,184,256,8,0x0d1628); ds.text(6,184,'FOLLOW THE GLOW',0x788ba8)
end
function inspect() return {x=px,y=py,grounded=grounded,score=score,frames=frames} end
