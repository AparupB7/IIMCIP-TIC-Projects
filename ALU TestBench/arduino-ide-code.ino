#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// --- Pin map ---
const uint8_t RES_PINS[8] = {0, 1, 2, 3, 6, 7, 10, 11}; // R0..R7
const uint8_t PIN_Z = 18, PIN_C = 19, PIN_V = 20, PIN_N = 21;
const uint8_t BUZZER_PIN = 4; // drives NPN transistor base via 1k
const uint8_t SDA_PIN = 22, SCL_PIN = 23;
// --- OLED ---
#define SCREEN_W 128
#define SCREEN_H 64
Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, -1);
bool oledOK = false;
// --- Web server ---
WebServer server(80);
const char *OP_NAMES[9] = {"ADD", "SUB", "AND", "OR", "XOR", "NOT",
"SHL", "SHR", "CMP"};
// --- ALU ---
struct AluOut {
uint8_t res;
bool z, c, v, n;
};
AluOut alu(uint8_t a, uint8_t b, uint8_t op) {
AluOut o = {0, false, false, false, false};
uint16_t t;
switch (op) {
case 0: // ADD
t = (uint16_t)a + (uint16_t)b;
o.res = (uint8_t)(t & 0xFF);
o.c = (t > 0xFF);
o.v = (((a ^ o.res) & (b ^ o.res) & 0x80) != 0);
break;
case 1: // SUB
case 8: // CMP (same flags as SUB)
t = (uint16_t)((int)a - (int)b);
o.res = (uint8_t)(t & 0xFF);
o.c = (a < b); // C = borrow
o.v = (((a ^ b) & (a ^ o.res) & 0x80) != 0);
break;
case 2: o.res = a & b; break; // AND
case 3: o.res = a | b; break; // OR
case 4: o.res = a ^ b; break; // XOR
case 5: o.res = (uint8_t)(~a); break; // NOT (of A)
case 6: // SHL (A << 1)
o.res = (uint8_t)(a << 1);
o.c = ((a & 0x80) != 0);
break;
case 7: // SHR (A >> 1)
o.res = (uint8_t)(a >> 1);
o.c = ((a & 0x01) != 0);
break;
default: break;
}
o.z = (o.res == 0);
o.n = ((o.res & 0x80) != 0);
return o;
}
// --- Hardware helpers ---
void beep() {
for (int i = 0; i < 80; i++) { // 80 cycles x 500 us = 40
ms at 2 kHz
digitalWrite(BUZZER_PIN, HIGH);
delayMicroseconds(250);
digitalWrite(BUZZER_PIN, LOW);
delayMicroseconds(250);
}
}
void driveLeds(const AluOut &o) {
for (int i = 0; i < 8; i++) {
digitalWrite(RES_PINS[i], (o.res >> i) & 1);
}
digitalWrite(PIN_Z, o.z);
digitalWrite(PIN_C, o.c);
digitalWrite(PIN_V, o.v);
digitalWrite(PIN_N, o.n);
}
void toBinary(uint8_t v, char *buf) { // buf must hold 9 chars
for (int i = 0; i < 8; i++) {
buf[i] = ((v >> (7 - i)) & 1) ? '1' : '0';
}
buf[8] = '\0';
}
void showOled(uint8_t a, uint8_t b, uint8_t op, const AluOut &o) {
if (!oledOK) return;
char bin[9];
toBinary(o.res, bin);
display.clearDisplay();
display.setTextSize(1);
display.setTextColor(SSD1306_WHITE);
display.setCursor(0, 0);
display.printf("A:%3u B:%3u", a, b);
display.setCursor(0, 12);
display.printf("OP: %s", OP_NAMES[op]);
display.setCursor(0, 24);
display.printf("Res: %3u 0x%02X", o.res, o.res);
display.setCursor(0, 36);
display.printf("Bin: %s", bin);
display.setCursor(0, 50);
display.printf("Z:%d C:%d V:%d N:%d", o.z, o.c, o.v, o.n);
display.display();
}
// --- Web page (served from flash) ---
static const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">

<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ALU Test Bench</title>
<style>
body{font-family:Arial,sans-serif;background:#111;color:#eee;margin:
0;padding:10px;
max-width:480px;margin:auto}
h2{text-align:center;margin:6px 0}
.row{display:flex;gap:8px;margin:6px 0}
.fld{flex:1;padding:10px;border:2px solid
#444;border-radius:8px;background:#1c1c1c;
text-align:center}
.fld.sel{border-color:#4caf50}
.fld small{display:block;color:#aaa}
.fld b{font-size:22px}
select,button{font-size:18px;border-radius:8px;border:none;padding:1
2px}
select{flex:1;background:#222;color:#eee}
.pad{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin
:8px 0}
.pad button{background:#333;color:#fff}
.pad button:disabled{opacity:.3}
.pad button:active{background:#555}
#exec{background:#4caf50;color:#fff;flex:2}
#auto{background:#2196f3;color:#fff;flex:1}
.base{background:#444;color:#fff;flex:1}
.box{background:#1c1c1c;border-radius:8px;padding:10px;margin:8px
0;font-family:monospace}
table{width:100%;border-collapse:collapse;font-size:12px}
td,th{border-bottom:1px solid #333;padding:3px;text-align:center}
.pass{color:#4caf50}.fail{color:#f44336}
</style>
</head>
<body>
<h2>ALU Test Bench</h2>
<div class="row">
<div class="fld sel" id="fA" onclick="selectField('A')">
<small>Operand A</small><b id="vA">0</b><small id="dA">=
0</small></div>
<div class="fld" id="fB" onclick="selectField('B')">
<small>Operand B</small><b id="vB">0</b><small id="dB">=
0</small></div>
</div>
<div class="row">
<select id="op">
<option value="0">ADD</option><option value="1">SUB</option>
<option value="2">AND</option><option value="3">OR</option>
<option value="4">XOR</option><option value="5">NOT (A)</option>
<option value="6">SHL (A)</option><option value="7">SHR
(A)</option>
<option value="8">CMP</option>
</select>
<button class="base" id="baseBtn"
onclick="toggleBase()">DEC</button>
</div>
<div class="pad" id="pad"></div>

<div class="row">
<button id="exec" onclick="onExec()">EXECUTE</button>
<button id="auto" onclick="onAuto()">AUTO-TEST</button>
</div>
<div class="box" id="out">Result: -</div>
<div class="box" id="sum">Auto-test: not run</div>
<table id="tbl"></table>
<script>
var sel = 'A', base = 10;
var txt = {A:'0', B:'0'};
var NAMES = ['ADD','SUB','AND','OR','XOR','NOT','SHL','SHR','CMP'];
function beep(){ fetch('/beep').catch(function(){}); }
function buildPad(){
var keys =
['7','8','9','A','4','5','6','B','1','2','3','C','0','D','E','F','DE
L','CLR'];
var pad = document.getElementById('pad');
pad.innerHTML = '';
keys.forEach(function(k){
var b = document.createElement('button');
b.textContent = k;
b.id = 'k' + k;
b.onclick = function(){ onKey(k); };
pad.appendChild(b);
});
refreshPad();
}
function refreshPad(){
'ABCDEF'.split('').forEach(function(k){
document.getElementById('k'+k).disabled = (base === 10);
});
}
function val(f){ var n = parseInt(txt[f], base); return isNaN(n) ? 0
: n; }
function refresh(){
document.getElementById('vA').textContent = txt.A;
document.getElementById('vB').textContent = txt.B;
document.getElementById('dA').textContent = '= ' + val('A');
document.getElementById('dB').textContent = '= ' + val('B');
document.getElementById('fA').className = 'fld' + (sel==='A' ? '
sel' : '');
document.getElementById('fB').className = 'fld' + (sel==='B' ? '
sel' : '');
}
function selectField(f){ beep(); sel = f; refresh(); }
function toggleBase(){
beep();
var a = val('A'), b = val('B');
base = (base === 10) ? 16 : 10;
txt.A = a.toString(base).toUpperCase();
txt.B = b.toString(base).toUpperCase();
document.getElementById('baseBtn').textContent = (base === 10) ?
'DEC' : 'HEX';

refreshPad();
refresh();
}
function onKey(k){
beep();
if (k === 'CLR'){ txt[sel] = '0'; }
else if (k === 'DEL'){
txt[sel] = txt[sel].slice(0, -1);
if (txt[sel] === '') txt[sel] = '0';
} else {
var t = (txt[sel] === '0') ? k : txt[sel] + k;
var n = parseInt(t, base);
if (!isNaN(n) && n <= 255) txt[sel] = t;
}
refresh();
}
function bin8(v){ var s = v.toString(2); while (s.length < 8) s =
'0' + s; return s; }
// Independent software model used as the "verification engineer"
function model(a, b, op){
var r = 0, c = 0, v = 0, ss;
var sa = a > 127 ? a - 256 : a;
var sb = b > 127 ? b - 256 : b;
switch (op){
case 0:
r = (a + b) & 255; c = (a + b > 255) ? 1 : 0;
ss = sa + sb; v = (ss > 127 || ss < -128) ? 1 : 0; break;
case 1: case 8:
r = (a - b) & 255; c = (a < b) ? 1 : 0;
ss = sa - sb; v = (ss > 127 || ss < -128) ? 1 : 0; break;
case 2: r = a & b; break;
case 3: r = a | b; break;
case 4: r = a ^ b; break;
case 5: r = (~a) & 255; break;
case 6: r = (a << 1) & 255; c = (a & 128) ? 1 : 0; break;
case 7: r = a >> 1; c = a & 1; break;
}
return {res:r, z:(r === 0 ? 1 : 0), c:c, v:v, n:((r & 128) ? 1 :
0)};
}
function fl(o){ return 'Z'+o.z+' C'+o.c+' V'+o.v+' N'+o.n; }
function same(g, e){
return (g.res === e.res && g.z === e.z && g.c === e.c && g.v ===
e.v && g.n === e.n);
}
async function runAlu(a, b, op){
var r = await fetch('/run?a=' + a + '&b=' + b + '&op=' + op);
return await r.json();
}
async function onExec(){
beep();
var a = val('A'), b = val('B'), op =
parseInt(document.getElementById('op').value);
var out = document.getElementById('out');
try {

var got = await runAlu(a, b, op);
var exp = model(a, b, op);
var ok = same(got, exp);
out.innerHTML = NAMES[op] + ' ' + a + ', ' + b + '<br>' +
'Result: ' + got.res + ' (0x' +
got.res.toString(16).toUpperCase() + ', ' +
bin8(got.res) + ')<br>' + 'Flags: ' + fl(got) + '<br>' +
'<span class="' + (ok ? 'pass' : 'fail') + '">' +
(ok ? 'MATCHES MODEL' : 'MISMATCH, expected ' + exp.res + ' '
+ fl(exp)) + '</span>';
} catch (e){
out.textContent = 'Connection error';
}
}
function sleep(ms){ return new Promise(function(r){ setTimeout(r,
ms); }); }
async function onAuto(){
beep();
var V = [
[0,0,0],[255,1,0],[255,255,0],[127,1,0],[128,128,0],[100,27,0],
[0,0,1],[0,1,1],[128,1,1],[127,255,1],[5,5,1],[200,100,1],
[255,0,2],[170,85,2],[255,255,2],
[0,0,3],[170,85,3],
[255,255,4],[170,85,4],
[0,0,5],[255,0,5],[170,0,5],
[128,0,6],[1,0,6],[255,0,6],
[1,0,7],[255,0,7],[0,0,7],
[5,5,8],[3,7,8],[9,2,8]
];
var tbl = document.getElementById('tbl');
tbl.innerHTML =
'<tr><th>Op</th><th>A</th><th>B</th><th>Expected</th>' +
'<th>Actual</th><th></th></tr>';

var pass = 0;
for (var i = 0; i < V.length; i++){
var a = V[i][0], b = V[i][1], op = V[i][2];
document.getElementById('sum').textContent = 'Running ' + (i+1)
+ '/' + V.length + '...';
try {
var got = await runAlu(a, b, op);
var exp = model(a, b, op);
var ok = same(got, exp);
if (ok) pass++;
var tr = document.createElement('tr');
tr.innerHTML = '<td>' + NAMES[op] + '</td><td>' + a +
'</td><td>' + b + '</td><td>' +

exp.res + ' ' + fl(exp) + '</td><td>' + got.res + ' ' +

fl(got) +

'</td><td class="' + (ok ? 'pass' : 'fail') + '">' + (ok ?

'PASS' : 'FAIL') + '</td>';
tbl.appendChild(tr);
} catch (e){
var tr2 = document.createElement('tr');
tr2.innerHTML = '<td colspan="6" class="fail">Connection
error</td>';
tbl.appendChild(tr2);
}
await sleep(400);
}

document.getElementById('sum').innerHTML = 'Auto-test: <span
class="' +
(pass === V.length ? 'pass' : 'fail') + '">' + pass + '/' +
V.length + ' passed</span>';
}
buildPad();
refresh();
</script>
</body>
</html>
)rawliteral";
// --- HTTP handlers ---
void handleRoot() {
server.send_P(200, "text/html", PAGE);
}
void handleBeep() {
beep();
server.send(200, "text/plain", "ok");
}
void handleRun() {
if (!server.hasArg("a") || !server.hasArg("b") ||
!server.hasArg("op")) {
server.send(400, "text/plain", "missing args");
return;
}
int a = constrain(server.arg("a").toInt(), 0, 255);
int b = constrain(server.arg("b").toInt(), 0, 255);
int op = constrain(server.arg("op").toInt(), 0, 8);
AluOut o = alu((uint8_t)a, (uint8_t)b, (uint8_t)op);
driveLeds(o);
showOled((uint8_t)a, (uint8_t)b, (uint8_t)op, o);
char bin[9];
toBinary(o.res, bin);
Serial.printf("%s A=%d B=%d -> RES=%d (0x%02X, %s) Z=%d C=%d V=%d
N=%d\n",

OP_NAMES[op], a, b, o.res, o.res, bin, o.z, o.c,

o.v, o.n);
char json[96];
snprintf(json, sizeof(json),

"{\"res\":%u,\"z\":%d,\"c\":%d,\"v\":%d,\"n\":%d}",
o.res, o.z ? 1 : 0, o.c ? 1 : 0, o.v ? 1 : 0, o.n ? 1 :

0);
server.send(200, "application/json", json);
}
void handleNotFound() {
server.send(404, "text/plain", "not found");
}
// --- Setup / loop ---
void setup() {
Serial.begin(115200);
delay(300);
for (int i = 0; i < 8; i++) {

pinMode(RES_PINS[i], OUTPUT);
digitalWrite(RES_PINS[i], LOW);
}
pinMode(PIN_Z, OUTPUT); digitalWrite(PIN_Z, LOW);
pinMode(PIN_C, OUTPUT); digitalWrite(PIN_C, LOW);
pinMode(PIN_V, OUTPUT); digitalWrite(PIN_V, LOW);
pinMode(PIN_N, OUTPUT); digitalWrite(PIN_N, LOW);
pinMode(BUZZER_PIN, OUTPUT);
digitalWrite(BUZZER_PIN, LOW);
Wire.begin(SDA_PIN, SCL_PIN);
oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
if (!oledOK) {
Serial.println("OLED not found (check wiring / address 0x3C).
Continuing without it.");
}
WiFi.mode(WIFI_AP);
WiFi.setSleep(false);
WiFi.softAP("ALU-Bench", "12345678", 1, 0, 4); // channel 1,
visible, max 4 clients
IPAddress ip = WiFi.softAPIP();
Serial.print("Wi-Fi: ALU-Bench | Open http://");
Serial.println(ip);
if (oledOK) {
display.clearDisplay();
display.setTextSize(1);
display.setTextColor(SSD1306_WHITE);
display.setCursor(0, 0);
display.println("ALU Test Bench");
display.println();
display.println("WiFi: ALU-Bench");
display.println("Pass: 12345678");
display.println();
display.print("Open ");
display.println(ip);
display.display();
}
server.on("/", handleRoot);
server.on("/run", handleRun);
server.on("/beep", handleBeep);
server.on("/ping", []() { server.send(200, "text/plain", "ESP32
OK"); });
server.onNotFound(handleNotFound);
server.begin();
beep(); // startup beep
}
void loop() {
server.handleClient();
delay(1);
}
