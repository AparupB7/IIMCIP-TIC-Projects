#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
const char* WIFI_SSID = "your-wifi-name/ssid";
const char* WIFI_PASS = "your-wifi-password";
#define BTN_LEFT 2

#define BTN_RIGHT 3
#define BUZZER 10
#define SDA_PIN 6
#define SCL_PIN 7
WebServer server(80);
float rollOffset = 0, pitchOffset = 0;
float roll = 0, pitch = 0;
unsigned long leftCount = 0, rightCount = 0;
bool lastL = HIGH, lastR = HIGH;
unsigned long lastLTime = 0, lastRTime = 0;
unsigned long buzzOffAt = 0;
// ---------------- WEB PAGE ----------------
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 3D Flight Game</title>
<style>
body{margin:0;overflow:hidden;background:#6fa8ec;font-family:sans-serif}
canvas{position:fixed;inset:0;width:100%;height:100%}
#hud{position:fixed;top:8px;left:0;right:0;text-align:center;color:#fff;fo
nt-size:20px;
text-shadow:0 1px 4px #000;pointer-events:none}
#ctl{position:fixed;bottom:8px;left:0;right:0;text-align:center}
#ctl button,#over button{padding:8px 16px;font-size:16px;margin:4px}
#tip{color:#fff;font-size:12px;text-shadow:0 1px 3px #000}
#flash{position:fixed;inset:0;background:#f00;opacity:0;pointer-events:non
e;transition:opacity .25s}

#over{position:fixed;inset:0;background:rgba(0,0,0,.6);color:#fff;display:
none;
flex-direction:column;align-items:center;justify-content:center;font-size:
24px}
#over h1{font-size:48px;margin:0 0 8px}
</style>
</head>
<body>
<canvas id="c"></canvas>
<div id="hud">Score: <span id="sc">0</span> &nbsp; <span id="lv"
style="color:#ff5c6c"></span> &nbsp; <span
id="st">connecting...</span></div>
<div id="ctl">
<button onclick="fetch('/cal')">Calibrate (hold level)</button>
<button onclick="reset()">Restart</button>
<div id="tip">Keyboard test: arrows = tilt, Z = left gun, X = right
gun</div>
</div>
<div id="flash"></div>
<div id="over"><h1>GAME OVER</h1><div id="fs"></div><button
onclick="reset()">Play again</button></div>
<script>
const $=id=>document.getElementById(id);
const cv=$('c'),ctx=cv.getContext('2d');
const SPEED=45,GY=-7,CAMZ=11;
const FOG=[168,203,228];
const LD=(()=>{const
v=[0.45,0.85,0.55],l=Math.hypot(v[0],v[1],v[2]);return
[v[0]/l,v[1]/l,v[2]/l];})();
let W=1,H=1,F=1,HY=1,xLim=9,camX=0,camY=3;
function resize(){
const d=Math.min(devicePixelRatio||1,2);
W=innerWidth;H=innerHeight;
cv.width=W*d;cv.height=H*d;

ctx.setTransform(d,0,0,d,0,0);
F=Math.min(H*0.95,W*1.3);
HY=H*0.38;
xLim=Math.max(2.5,Math.min(9,8.8*W/F));
}
addEventListener('resize',resize);resize();
// ---------- math helpers ----------
const clamp=(v,a,b)=>v<a?a:v>b?b:v;
const sub=(a,b)=>[a[0]-b[0],a[1]-b[1],a[2]-b[2]];
const
cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
;
function norm(v){const l=Math.hypot(v[0],v[1],v[2])||1;return
[v[0]/l,v[1]/l,v[2]/l];}
// ---------- model builders ----------
function convex(polys,col){
const all=[].concat.apply([],polys);
const c=[0,0,0];
all.forEach(p=>{c[0]+=p[0];c[1]+=p[1];c[2]+=p[2];});
c[0]/=all.length;c[1]/=all.length;c[2]/=all.length;
return polys.map((pts,i)=>{
let n=norm(cross(sub(pts[1],pts[0]),sub(pts[2],pts[0])));
const pc=[0,0,0];
pts.forEach(p=>{pc[0]+=p[0];pc[1]+=p[1];pc[2]+=p[2];});
pc[0]/=pts.length;pc[1]/=pts.length;pc[2]/=pts.length;
if(n[0]*(pc[0]-c[0])+n[1]*(pc[1]-c[1])+n[2]*(pc[2]-c[2])<0)n=[-n[0],-n[1],
-n[2]];
return {p:pts,n:n,c:typeof col==='function'?col(i):col};
});
}
function extrude(pts,d,col){
const top=pts.map(p=>[p[0]+d[0],p[1]+d[1],p[2]+d[2]]);
const polys=[pts,top];
for(let i=0;i<pts.length;i++){const
j=(i+1)%pts.length;polys.push([pts[i],pts[j],top[j],top[i]]);}

return convex(polys,col);
}
function loft(rings,sides,colFn,ys){
const out=[];ys=ys||1;
const V=(r,k)=>{const a=k/sides*Math.PI*2;return
[(r.cx||0)+Math.cos(a)*r.r,(r.cy||0)+Math.sin(a)*r.r*ys,r.z];};
for(let i=0;i<rings.length-1;i++){
const a=rings[i],b=rings[i+1],slope=-(b.r-a.r)/(b.z-a.z);
for(let k=0;k<sides;k++){
const am=(k+0.5)/sides*Math.PI*2;
out.push({p:[V(a,k),V(a,k+1),V(b,k+1),V(b,k)],n:norm([Math.cos(am),Math.si
n(am),slope]),c:colFn(i,k)});
}
}
return out;
}
function box(x,y,z,w,h,d,col){
const x0=x-w/2,x1=x+w/2,y0=y,y1=y+h,z0=z-d/2,z1=z+d/2;
const
P=[[x0,y0,z0],[x1,y0,z0],[x1,y1,z0],[x0,y1,z0],[x0,y0,z1],[x1,y0,z1],[x1,y
1,z1],[x0,y1,z1]];
return
convex([[P[0],P[1],P[2],P[3]],[P[4],P[5],P[6],P[7]],[P[0],P[1],P[5],P[4]],
[P[3],P[2],P[6],P[7]],[P[0],P[3],P[7],P[4]],[P[1],P[2],P[6],P[5]]],col);
}
function cone(x,y,z,r,h,n,col){
const base=[];
for(let k=0;k<n;k++){const
a=k/n*6.2832;base.push([x+Math.cos(a)*r,y,z+Math.sin(a)*r]);}
const apex=[x,y+h,z];
return convex(base.map((p,i)=>[p,base[(i+1)%n],apex]),col);
}
function mountain(R,h){
const n=5,off=Math.random()*6,lo=[],hi=[];
for(let k=0;k<n;k++){
const a=off+k/n*6.2832;

lo.push([Math.cos(a)*R,0,Math.sin(a)*R]);
hi.push([Math.cos(a)*R*0.3,h*0.7,Math.sin(a)*R*0.3]);
}
const apex=[0,h,0],polys=[];
for(let k=0;k<n;k++){const
j=(k+1)%n;polys.push([lo[k],lo[j],hi[j],hi[k]]);}
for(let k=0;k<n;k++){const j=(k+1)%n;polys.push([hi[k],hi[j],apex]);}
return convex(polys,i=>i<n?[112,106,96]:[240,244,250]);
}
// ---------- jet model (nose toward -Z, up = +Y) ----------
function buildJet(P,guns){
const f=[];
const
fus=[{z:-3.4,r:0.03},{z:-2.8,r:0.2},{z:-2,r:0.34},{z:-1,r:0.46},{z:0.6,r:0
.5},{z:1.8,r:0.42},{z:2.3,r:0.36}];
f.push.apply(f,loft(fus,8,i=>(i<2||i===5)?P.acc:P.body,0.85));
const
can=[{z:-1.9,r:0.02,cy:0.34},{z:-1.4,r:0.2,cy:0.34},{z:-0.6,r:0.27,cy:0.34
},{z:0.3,r:0.22,cy:0.34},{z:0.9,r:0.02,cy:0.34}];
f.push.apply(f,loft(can,6,()=>P.glass,0.9));
[1,-1].forEach(s=>{
const
w=[[0.4,-0.05,-0.5],[3.5,-0.05,1.1],[3.5,-0.05,1.6],[0.4,-0.05,1.8]].map(p
=>[p[0]*s,p[1],p[2]]);
f.push.apply(f,extrude(w,[0,0.07,0],P.acc));
const
t=[[0.35,0,1.5],[1.6,0,2.3],[1.6,0,2.6],[0.35,0,2.6]].map(p=>[p[0]*s,p[1],
p[2]]);
f.push.apply(f,extrude(t,[0,0.05,0],P.acc));
});
f.push.apply(f,extrude([[-0.04,0.35,1.0],[-0.04,1.5,2.0],[-0.04,1.5,2.5],[
-0.04,0.35,2.5]],[0.08,0,0],P.acc));
f.push.apply(f,loft([{z:2.2,r:0.37},{z:2.6,r:0.31},{z:2.65,r:0.25}],6,()=>
P.dark));
if(guns){

[1,-1].forEach(s=>{
const cx=1.8*s,cy=-0.12;
f.push.apply(f,loft([{z:-1.5,r:0.04,cx:cx,cy:cy},{z:-1.3,r:0.13,cx:cx,cy:c
y},{z:0.5,r:0.13,cx:cx,cy:cy},{z:0.7,r:0.05,cx:cx,cy:cy}],6,()=>P.body));
f.push.apply(f,loft([{z:-2.2,r:0.045,cx:cx,cy:cy},{z:-1.5,r:0.045,cx:cx,cy
:cy}],6,()=>P.dark));
});
}
return f;
}
function flameFaces(){
const
mk=(rings,col)=>loft(rings,6,()=>col).map(q=>({p:q.p,c:col,e:true,fl:true}
));
return mk([{z:2.65,r:0.22},{z:3.3,r:0.16},{z:4.2,r:0.01}],[255,150,40])
.concat(mk([{z:2.65,r:0.12},{z:3.0,r:0.09},{z:3.6,r:0.01}],[255,235,160]))
;
}
const
JET_P=buildJet({body:[205,215,230],acc:[55,105,205],glass:[90,190,255],dar
k:[45,48,55]},true);
const
JET_E=buildJet({body:[125,40,40],acc:[215,50,50],glass:[255,190,80],dark:[
40,30,30]},false);
const FLAME=flameFaces();
// ---------- transforms ----------
function mkT(x,y,z,rx,ry,rz,s,fs){
return {x:x,y:y,z:z,s:s||1,fs:fs||1,rot:true,
cx:Math.cos(rx),sx:Math.sin(rx),cy:Math.cos(ry),sy:Math.sin(ry),cz:Math.co
s(rz),sz:Math.sin(rz)};
}
function rot3(x,y,z,t){
let a=x*t.cz-y*t.sz,b=x*t.sz+y*t.cz;x=a;y=b;

a=y*t.cx-z*t.sx;b=y*t.sx+z*t.cx;y=a;z=b;
a=x*t.cy+z*t.sy;b=-x*t.sy+z*t.cy;x=a;z=b;
return [x,y,z];
}
function xf(p,t){
let x=p[0],y=p[1],z=p[2];
if(t.s!==1){x*=t.s;y*=t.s;z*=t.s;}
if(t.rot){const r=rot3(x,y,z,t);x=r[0];y=r[1];z=r[2];}
return [x+t.x,y+t.y,z+t.z];
}
function proj(x,y,z){
const dz=CAMZ-z;if(dz<0.5)return null;
const k=F/dz;
return [W/2+(x-camX)*k,HY-(y-camY)*k,k];
}
// ---------- software 3D renderer (painter's algorithm) ----------
const pool=[];
function emit(faces,t){
for(let fi=0;fi<faces.length;fi++){
const f=faces[fi];
let n=null;
if(f.n)n=t.rot?rot3(f.n[0],f.n[1],f.n[2],t):f.n;
const pts=[];let cx=0,cy=0,cz=0;
for(let i=0;i<f.p.length;i++){
let p=f.p[i];
if(f.fl)p=[p[0],p[1],2.65+(p[2]-2.65)*t.fs];
const w=xf(p,t);
cx+=w[0];cy+=w[1];cz+=w[2];pts.push(w);
}
const m=pts.length;cx/=m;cy/=m;cz/=m;
const dx=camX-cx,dy=camY-cy,dz=CAMZ-cz;
if(n&&n[0]*dx+n[1]*dy+n[2]*dz<=0)continue;
const sp=[];let bad=false,minx=1e9,maxx=-1e9,miny=1e9,maxy=-1e9;
for(let i=0;i<m;i++){
const w=pts[i],dzz=CAMZ-w[2];
if(dzz<0.6){bad=true;break;}
const k=F/dzz,sx=W/2+(w[0]-camX)*k,sy=HY-(w[1]-camY)*k;

sp.push(sx,sy);
if(sx<minx)minx=sx;if(sx>maxx)maxx=sx;if(sy<miny)miny=sy;if(sy>maxy)maxy=s
y;
}
if(bad||maxx<0||minx>W||maxy<0||miny>H)continue;
const d2=dx*dx+dy*dy+dz*dz;
let r,g,b;
if(f.e){r=f.c[0];g=f.c[1];b=f.c[2];}
else{
const lam=Math.max(0,n[0]*LD[0]+n[1]*LD[1]+n[2]*LD[2]);
const sh=Math.min(1.1,0.34+0.66*lam+0.1*(n[1]*0.5+0.5));
r=f.c[0]*sh;g=f.c[1]*sh;b=f.c[2]*sh;
const fg=clamp((Math.sqrt(d2)-60)/180,0,1)*0.92;
r+=(FOG[0]-r)*fg;g+=(FOG[1]-g)*fg;b+=(FOG[2]-b)*fg;
}
pool.push({d:d2,s:sp,c:'rgb('+(r|0)+','+(g|0)+','+(b|0)+')'});
}
}
// ---------- glow sprites ----------
function mkGlow(r,g,b){
const c=document.createElement('canvas');c.width=c.height=64;
const x=c.getContext('2d');
const gr=x.createRadialGradient(32,32,0,32,32,32);
gr.addColorStop(0,'rgba('+r+','+g+','+b+',1)');
gr.addColorStop(0.35,'rgba('+r+','+g+','+b+',0.5)');
gr.addColorStop(1,'rgba('+r+','+g+','+b+',0)');
x.fillStyle=gr;x.fillRect(0,0,64,64);
return c;
}
const
glowFire=mkGlow(255,170,60),glowWhite=mkGlow(255,240,200),glowSmoke=mkGlow
(70,70,70);
const
cloudSpr=document.createElement('canvas');cloudSpr.width=256;cloudSpr.heig
ht=128;

(function(){
const c=cloudSpr.getContext('2d');
[[70,78,46],[120,60,54],[175,74,46],[100,86,40],[150,88,40],[210,90,32],[4
0,92,30]].forEach(b=>{
const g=c.createRadialGradient(b[0],b[1],0,b[0],b[1],b[2]);
g.addColorStop(0,'rgba(255,255,255,0.95)');g.addColorStop(0.6,'rgba(255,25
5,255,0.55)');g.addColorStop(1,'rgba(255,255,255,0)');
c.fillStyle=g;c.beginPath();c.arc(b[0],b[1],b[2],0,7);c.fill();
});
c.globalCompositeOperation='source-atop';
const sh=c.createLinearGradient(0,40,0,128);
sh.addColorStop(0,'rgba(255,255,255,0)');sh.addColorStop(1,'rgba(150,170,2
00,0.55)');
c.fillStyle=sh;c.fillRect(0,0,256,128);
})();
// ---------- scenery ----------
const
treeM=(s,col)=>box(0,0,0,0.35*s,1.4*s,0.35*s,[95,65,40]).concat(cone(0,1.2
*s,0,1.5*s,4*s,6,col));
const
TREES=[treeM(1,[34,110,50]),treeM(1.5,[28,95,45]),treeM(1.2,[48,125,55])];
const
BLDGS=[box(0,0,0,4,6,4,[150,155,165]),box(0,0,0,3,10,3,[120,130,150]),box(
0,0,0,6,3,5,[175,150,130])];
const sgn=()=>Math.random()<0.5?-1:1;
const scenery=[];
function addScn(f,rx,zmin,zmax){
scenery.push({f:f,rx:rx,x:rx(),z:zmin-Math.random()*(zmax-zmin)});
}
for(let
i=0;i<42;i++)addScn(TREES[i%3],()=>sgn()*(5+Math.random()*70),10,-260);
for(let
i=0;i<14;i++)addScn(BLDGS[i%3],()=>sgn()*(14+Math.random()*60),10,-260);

for(let
i=0;i<14;i++)addScn(mountain(10+Math.random()*10,16+Math.random()*16),()=>
sgn()*(45+Math.random()*120),10,-260);
const clouds=[];
for(let
i=0;i<26;i++)clouds.push({x:(Math.random()-0.5)*420,y:16+Math.random()*26,
z:-Math.random()*260,s:0.7+Math.random()*0.9});
let scroll=0;
// ---------- sky / ground / clouds ----------
function drawSky(){
const sg=ctx.createLinearGradient(0,0,0,HY);
sg.addColorStop(0,'#1f5fc7');sg.addColorStop(0.65,'#6fa8ec');sg.addColorSt
op(1,'#cfe6f7');
ctx.fillStyle=sg;ctx.fillRect(0,0,W,HY+1);
const sx=W*0.74,sy=HY*0.5,sr=H*0.2;
const g=ctx.createRadialGradient(sx,sy,0,sx,sy,sr);
g.addColorStop(0,'rgba(255,250,215,1)');g.addColorStop(0.15,'rgba(255,240,
190,0.85)');g.addColorStop(1,'rgba(255,230,160,0)');
ctx.fillStyle=g;ctx.fillRect(sx-sr,sy-sr,sr*2,sr*2);
}
function drawGround(){
const dy=camY-GY;
const g=ctx.createLinearGradient(0,HY,0,H);
g.addColorStop(0,'rgb(168,203,228)');g.addColorStop(0.3,'rgb(80,135,80)');
g.addColorStop(1,'rgb(44,104,48)');
ctx.fillStyle=g;ctx.fillRect(0,HY,W,H-HY);
const BW=14,base=Math.floor(scroll/BW),off=scroll-base*BW;
for(let k=0;k>-20;k--){
const zn=k*BW+off,zf=zn-BW;
const dn=Math.max(1,CAMZ-zn),df=CAMZ-zf;
const yn=Math.min(H+2,HY+dy*F/dn),yf=HY+dy*F/df;
if(yf>=yn)continue;
const id=k-1-base;
const rr=(((id*9301+49297)%233280)+233280)%233280/233280;

const odd=(id&1)?1:0;
const c=[52+odd*12+rr*14,118+odd*14+rr*16,54+odd*8];
const fg=clamp((df-60)/180,0,1)*0.92;
ctx.fillStyle='rgb('+((c[0]+(FOG[0]-c[0])*fg)|0)+','+((c[1]+(FOG[1]-c[1])*
fg)|0)+','+((c[2]+(FOG[2]-c[2])*fg)|0)+')';
ctx.fillRect(0,yf,W,yn-yf+1);
}
ctx.strokeStyle='rgba(15,55,20,0.18)';ctx.lineWidth=2;ctx.beginPath();
const kf=F/(CAMZ+250),kn=F/6;
for(let x=-420;x<=420;x+=30){
ctx.moveTo(W/2+(x-camX)*kf,HY+dy*kf);
ctx.lineTo(W/2+(x-camX)*kn,HY+dy*kn);
}
ctx.stroke();
const hz=ctx.createLinearGradient(0,HY,0,HY+H*0.12);
hz.addColorStop(0,'rgba(190,220,240,0.95)');hz.addColorStop(1,'rgba(190,22
0,240,0)');
ctx.fillStyle=hz;ctx.fillRect(0,HY,W,H*0.12);
}
function drawClouds(){
for(const c of clouds){
const dz=CAMZ-c.z;if(dz<22)continue;
const k=F/dz,w=60*c.s*k,h=w/2;
const sx=W/2+(c.x-camX)*k,sy=HY-(c.y-camY)*k;
if(sx+w<0||sx-w>W||sy+h<0||sy-h>H)continue;
ctx.globalAlpha=clamp((dz-22)/20,0,1)*(1-clamp((dz-200)/60,0,1))*0.95;
ctx.drawImage(cloudSpr,sx-w/2,sy-h/2,w,h);
}
ctx.globalAlpha=1;
}
// ---------- game state ----------
const ST={s:1,fs:1,rot:false,x:0,y:0,z:0};
const P={x:0,y:0,rx:0,rz:0};
let bullets=[],enemies=[],parts=[];
let score=0,lives=3,over=false,dead=false,spawnT=1,shake=0;

let roll=0,pitch=0,lastL=0,lastR=0,first=true;
const gf={L:0,R:0};
const keys={};
addEventListener('keydown',e=>{
keys[e.key]=1;
if(e.key==='z'||e.key==='Z')shoot('L');
if(e.key==='x'||e.key==='X')shoot('R');
});
addEventListener('keyup',e=>{keys[e.key]=0;});
function hud(){
$('sc').textContent=score;
$('lv').textContent='\u2665'.repeat(Math.max(0,lives));
}
function reset(){
bullets=[];enemies=[];parts=[];
P.x=0;P.y=0;P.rx=0;P.rz=0;
score=0;lives=3;over=false;dead=false;spawnT=1;
$('over').style.display='none';
hud();
}
reset();
function flash(){
const f=$('flash');f.style.opacity=0.5;
setTimeout(()=>{f.style.opacity=0;},120);
}
function shoot(side){
if(over||dead)return;
const T=mkT(P.x,P.y,0,P.rx,0,P.rz,1,1);
const w=xf([side==='L'?-1.8:1.8,-0.12,-2.25],T);
bullets.push({x:w[0],y:w[1],z:w[2],pz:w[2]});
gf[side]=0.07;
}
function spawnEnemy(){
enemies.push({x:(Math.random()*2-1)*xLim*1.05,y:-3.5+Math.random()*8.5,z:-
200,pz:-200,
v:28+Math.random()*12+score/25,ph:Math.random()*6});

}
function rdir(){
let x,y,z,l;
do{x=Math.random()*2-1;y=Math.random()*2-1;z=Math.random()*2-1;l=x*x+y*y+z
*z;}while(l>1||l<0.01);
l=Math.sqrt(l);return [x/l,y/l,z/l];
}
function explode(x,y,z,big){
parts.push({x:x,y:y,z:z,vx:0,vy:0,vz:0,life:0.25,max:0.25,s:big?7:5,t:'w'}
);
const n=big?28:20;
for(let i=0;i<n;i++){
const d=rdir(),sp=4+Math.random()*12,life=0.5+Math.random()*0.6;
parts.push({x:x,y:y,z:z,vx:d[0]*sp,vy:d[1]*sp,vz:d[2]*sp,life:life,max:lif
e,s:1.2+Math.random()*2,t:'f'});
}
for(let i=0;i<8;i++){
const d=rdir(),sp=1+Math.random()*4,life=1+Math.random();
parts.push({x:x,y:y,z:z,vx:d[0]*sp,vy:d[1]*sp,vz:d[2]*sp,life:life,max:lif
e,s:2+Math.random()*2.5,t:'s'});
}
}
// ---------- poll ESP32 ----------
async function poll(){
try{
const r=await fetch('/s');
const d=await r.json();
roll=d.r;pitch=d.p;
if(first){lastL=d.l;lastR=d.rt;first=false;}
let n=Math.min(3,d.l-lastL);for(let i=0;i<n;i++)shoot('L');
lastL=d.l;
n=Math.min(3,d.rt-lastR);for(let i=0;i<n;i++)shoot('R');
lastR=d.rt;

$('st').textContent='connected R:'+d.r+' P:'+d.p;
}catch(e){
$('st').textContent='disconnected';
}
setTimeout(poll,30);
}
poll();
// ---------- update ----------
function update(dt){
scroll+=SPEED*dt;
for(const o of scenery){
o.z+=SPEED*dt;
if(o.z>16){o.z-=280;o.x=o.rx();}
}
for(const c of clouds){c.z+=SPEED*dt;if(c.z>0)c.z-=270;}
let r=roll,p=pitch;
if(keys.ArrowLeft||keys.ArrowRight||keys.ArrowUp||keys.ArrowDown){
r=(keys.ArrowRight?25:0)-(keys.ArrowLeft?25:0);
p=(keys.ArrowDown?25:0)-(keys.ArrowUp?25:0);
}
if(!over){
const dz=v=>Math.abs(v)<4?0:v;
P.x+=dz(r)*0.5*dt;
P.y-=dz(p)*0.35*dt;
P.x=clamp(P.x,-xLim,xLim);
P.y=clamp(P.y,-3.5,5);
const tz=-clamp(r,-35,35)*0.017,tx=-clamp(p,-30,30)*0.012;
const k=Math.min(1,dt*8);
P.rz+=(tz-P.rz)*k;P.rx+=(tx-P.rx)*k;
spawnT-=dt;
if(spawnT<=0){spawnEnemy();spawnT=Math.max(0.45,1.1-score/700);}
}
gf.L=Math.max(0,gf.L-dt);gf.R=Math.max(0,gf.R-dt);
for(let i=bullets.length-1;i>=0;i--){
const b=bullets[i];b.pz=b.z;b.z-=160*dt;

if(b.z<-230)bullets.splice(i,1);
}
for(let i=enemies.length-1;i>=0;i--){
const e=enemies[i];
e.pz=e.z;e.z+=e.v*dt;e.ph+=dt*2;
let kill=false;
for(let j=bullets.length-1;j>=0;j--){
const b=bullets[j];
if(Math.abs(b.x-e.x)<2.2&&Math.abs(b.y-e.y)<1.4&&b.z<=e.z+2&&b.pz>=e.pz-2)
{
explode(e.x,e.y,e.z,false);
bullets.splice(j,1);kill=true;score+=10;break;
}
}
if(!kill&&!over&&Math.abs(e.x-P.x)<2.6&&Math.abs(e.y-P.y)<1.2&&e.z>-2.5&&e
.pz<2.5){
explode(e.x,e.y,e.z,true);
kill=true;lives--;shake=1;flash();
}
if(!kill&&e.z>8)kill=true; // escaped: no penalty
if(kill)enemies.splice(i,1);
}
for(let i=parts.length-1;i>=0;i--){
const q=parts[i];
q.life-=dt;
q.x+=q.vx*dt;q.y+=q.vy*dt;q.z+=q.vz*dt+SPEED*dt*0.8;
if(q.life<=0)parts.splice(i,1);
}
if(!over&&lives<=0){
over=true;dead=true;
explode(P.x,P.y,0,true);
$('fs').textContent='Score: '+score;
$('over').style.display='flex';
}
hud();

shake=Math.max(0,shake-dt*2.5);
camX=P.x*0.5+(Math.random()-0.5)*shake*0.6;
camY=3+P.y*0.3+(Math.random()-0.5)*shake*0.6;
}
// ---------- render ----------
function render(){
drawSky();drawGround();drawClouds();
pool.length=0;
for(const o of scenery){ST.x=o.x;ST.y=GY;ST.z=o.z;emit(o.f,ST);}
for(const e of
enemies)emit(JET_E,mkT(e.x,e.y,e.z,0,Math.PI,Math.sin(e.ph)*0.35,1.25,1));
let PT=null;
if(!dead){
PT=mkT(P.x,P.y,0,P.rx,0,P.rz,1,0.8+Math.random()*0.5);
emit(JET_P,PT);emit(FLAME,PT);
}
pool.sort((a,b)=>b.d-a.d);
ctx.lineJoin='round';ctx.lineWidth=1;
for(let i=0;i<pool.length;i++){
const q=pool[i],s=q.s;
ctx.fillStyle=q.c;ctx.strokeStyle=q.c;
ctx.beginPath();ctx.moveTo(s[0],s[1]);
for(let j=2;j<s.length;j+=2)ctx.lineTo(s[j],s[j+1]);
ctx.closePath();ctx.fill();ctx.stroke();
}
for(const q of parts){
if(q.t!=='s')continue;
const p3=proj(q.x,q.y,q.z);if(!p3)continue;
const a=q.life/q.max,sz=q.s*p3[2]*(1+(1-a)*1.2);
ctx.globalAlpha=a*0.45;
ctx.drawImage(glowSmoke,p3[0]-sz,p3[1]-sz,sz*2,sz*2);
}
ctx.globalAlpha=1;
ctx.globalCompositeOperation='lighter';
if(PT){
const w=xf([0,0,2.8],PT),p3=proj(w[0],w[1],w[2]);

if(p3){const
sz=1.3*p3[2];ctx.globalAlpha=0.75;ctx.drawImage(glowFire,p3[0]-sz,p3[1]-sz
,sz*2,sz*2);}
[['L',-1.8],['R',1.8]].forEach(a=>{
if(gf[a[0]]<=0)return;
const m=xf([a[1],-0.12,-2.3],PT),q=proj(m[0],m[1],m[2]);
if(q){const
sz=1.1*q[2];ctx.globalAlpha=1;ctx.drawImage(glowWhite,q[0]-sz,q[1]-sz,sz*2
,sz*2);}
});
}
ctx.lineCap='round';
for(const b of bullets){
const a=proj(b.x,b.y,b.z),c=proj(b.x,b.y,b.z+5);
if(!a||!c)continue;
ctx.globalAlpha=0.7;ctx.strokeStyle='rgb(255,170,60)';ctx.lineWidth=Math.m
ax(2.5,0.3*a[2]);
ctx.beginPath();ctx.moveTo(a[0],a[1]);ctx.lineTo(c[0],c[1]);ctx.stroke();
ctx.globalAlpha=1;ctx.strokeStyle='rgb(255,250,210)';ctx.lineWidth=Math.ma
x(1.2,0.12*a[2]);
ctx.beginPath();ctx.moveTo(a[0],a[1]);ctx.lineTo(c[0],c[1]);ctx.stroke();
}
for(const q of parts){
if(q.t==='s')continue;
const p3=proj(q.x,q.y,q.z);if(!p3)continue;
const a=clamp(q.life/q.max,0,1),sz=q.s*p3[2]*(0.4+0.6*a);
ctx.globalAlpha=a;
ctx.drawImage(q.t==='w'?glowWhite:glowFire,p3[0]-sz,p3[1]-sz,sz*2,sz*2);
}
ctx.globalAlpha=1;
ctx.globalCompositeOperation='source-over';
}

let last=performance.now();
function loop(now){
const dt=Math.min(0.05,(now-last)/1000);
last=now;
update(dt);
render();
requestAnimationFrame(loop);
}
requestAnimationFrame(loop);
</script>
</body>
</html>
)rawliteral";
// ---------------- MPU6050 ----------------
uint8_t mpuAddr = 0x68;
bool mpuOK = false;
int16_t rawAx = 0, rawAy = 0, rawAz = 0;
int lastErr = 0, lastGot = 0;
bool mpuWrite(uint8_t reg, uint8_t val) {
Wire.beginTransmission(mpuAddr);
Wire.write(reg);
Wire.write(val);
return Wire.endTransmission(true) == 0;
}
int mpuReadBytes(uint8_t reg, uint8_t* buf, int len) {
Wire.beginTransmission(mpuAddr);
Wire.write(reg);
lastErr = Wire.endTransmission(true); // normal STOP (not repeated
start)
if (lastErr != 0) return 0;
lastGot = Wire.requestFrom((int)mpuAddr, len);
int i = 0;
while (Wire.available() && i < len) buf[i++] = Wire.read();
return i;
}

void mpuInit() {
Wire.begin(SDA_PIN, SCL_PIN);
Wire.setClock(100000);
delay(100);
Serial.println("Scanning I2C bus...");
int found = 0;
for (uint8_t a = 1; a < 127; a++) {
Wire.beginTransmission(a);
if (Wire.endTransmission() == 0) {
Serial.printf(" device found at 0x%02X\n", a);
found++;
}
}
if (found == 0) Serial.println(" NOTHING found - check SDA/SCL
wires!");
uint8_t addrs[2] = {0x68, 0x69};
for (int i = 0; i < 2; i++) {
Wire.beginTransmission(addrs[i]);
if (Wire.endTransmission() == 0) { mpuAddr = addrs[i]; mpuOK = true;
break; }
}
if (!mpuOK) { Serial.println("MPU NOT FOUND at 0x68 or 0x69"); return; }
Serial.printf("MPU found at 0x%02X\n", mpuAddr);
uint8_t who = 0;
int n = mpuReadBytes(0x75, &who, 1);
Serial.printf("WHO_AM_I = 0x%02X (read %d byte)\n", who, n);
mpuWrite(0x6B, 0x80); // reset chip
delay(150);
mpuWrite(0x6B, 0x01); // wake up, use gyro clock
delay(100);
mpuWrite(0x6C, 0x00); // enable all accel/gyro axes
mpuWrite(0x1C, 0x00); // accel range +-2g
delay(50);

}
void mpuRead() {
if (!mpuOK) return;
uint8_t b[6];
if (mpuReadBytes(0x3B, b, 6) != 6) return;
rawAx = (int16_t)((b[0] << 8) | b[1]);
rawAy = (int16_t)((b[2] << 8) | b[3]);
rawAz = (int16_t)((b[4] << 8) | b[5]);
if (rawAx == 0 && rawAy == 0 && rawAz == 0) return; // no data yet
float rawRoll = atan2((float)rawAy, (float)rawAz) * 180.0 / PI;
float rawPitch = atan2(-(float)rawAx, sqrt((float)rawAy * rawAy +
(float)rawAz * rawAz)) * 180.0 / PI;
roll = 0.7 * roll + 0.3 * (rawRoll - rollOffset);
pitch = 0.7 * pitch + 0.3 * (rawPitch - pitchOffset);
}
void calibrate() {
float r = 0, p = 0;
rollOffset = 0; pitchOffset = 0;
for (int i = 0; i < 40; i++) { mpuRead(); delay(10); }
for (int i = 0; i < 30; i++) { mpuRead(); r += roll; p += pitch;
delay(10); }
rollOffset = r / 30.0;
pitchOffset = p / 30.0;
roll = 0; pitch = 0;
}
// ---------------- Buzzer ----------------
void beep() {
digitalWrite(BUZZER, HIGH);
buzzOffAt = millis() + 60;
}
// ---------------- Setup / Loop ----------------

void setup() {
Serial.begin(115200);
pinMode(BTN_LEFT, INPUT_PULLUP);
pinMode(BTN_RIGHT, INPUT_PULLUP);
pinMode(BUZZER, OUTPUT);
digitalWrite(BUZZER, LOW);
mpuInit();
delay(200);
calibrate();
// ---- WiFi connect with diagnostics ----
WiFi.mode(WIFI_STA);
WiFi.disconnect(true);
delay(500);
Serial.println("\nScanning networks (2.4 GHz only)...");
int n = WiFi.scanNetworks();
for (int i = 0; i < n; i++) {
Serial.printf(" %d: %s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(),
WiFi.RSSI(i));
}
WiFi.begin(WIFI_SSID, WIFI_PASS);
Serial.print("Connecting");
int tries = 0;
while (WiFi.status() != WL_CONNECTED) {
delay(500);
Serial.print(".");
if (++tries % 20 == 0) {
Serial.printf("\nStatus code: %d - retrying\n", WiFi.status());
WiFi.disconnect();
WiFi.begin(WIFI_SSID, WIFI_PASS);
}
}
Serial.println();
Serial.print("Open this in your browser: http://");
Serial.println(WiFi.localIP());

server.on("/", []() { server.send_P(200, "text/html", PAGE); });
server.on("/s", []() {
String j = "{\"r\":" + String(roll, 1) + ",\"p\":" + String(pitch, 1)
+
",\"l\":" + String(leftCount) + ",\"rt\":" +
String(rightCount) + "}";
server.send(200, "application/json", j);
});
server.on("/cal", []() {
calibrate();
server.send(200, "text/plain", "ok");
});
server.begin();
}
void loop() {
server.handleClient();
mpuRead();
unsigned long now = millis();
// Left button (falling edge = press)
bool l = digitalRead(BTN_LEFT);
if (l == LOW && lastL == HIGH && now - lastLTime > 40) {
leftCount++;
beep();
lastLTime = now;
}
lastL = l;
// Right button
bool r = digitalRead(BTN_RIGHT);
if (r == LOW && lastR == HIGH && now - lastRTime > 40) {
rightCount++;
beep();
lastRTime = now;
}

lastR = r;
// Turn buzzer off after 60 ms
if (buzzOffAt && now >= buzzOffAt) {
digitalWrite(BUZZER, LOW);
buzzOffAt = 0;
}
// Debug print every 0.5 s
static unsigned long lastPrint = 0;
if (now - lastPrint > 500) {
lastPrint = now;
Serial.printf("ax=%d ay=%d az=%d | roll=%.1f pitch=%.1f | err=%d
got=%d\n",
rawAx, rawAy, rawAz, roll, pitch, lastErr, lastGot);
}
}
