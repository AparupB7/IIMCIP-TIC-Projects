#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
// ---------- WiFi Credentials (EDIT THESE) ----------
const char* WIFI_SSID = "your-wifi-ssid";
const char* WIFI_PASS = "your-wifi-password";
WebServer server(80);
// ---------- Pin Definitions ----------
#define MQ2_PIN 2
#define DHT_PIN 4
#define BUZZER_PIN 5
#define SDA_PIN 8
#define SCL_PIN 9
#define DHT_TYPE DHT11
// ---------- LCD Config ----------
#define LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// ---------- Circuit / ADC Constants ----------
const float ADC_MAX = 4095.0;
const float ADC_VREF = 3.3;
const float DIVIDER_RATIO = 20000.0 / (10000.0 + 20000.0);
const float MQ2_VCC = 5.0;
const float RL_VALUE = 5.0;
const float RATIO_CLEAN_AIR = 9.83;
const float LPG_A = 574.25, LPG_B = -2.222;
const float CO_A = 599.65, CO_B = -2.244;
const float CH4_A = 1013.15, CH4_B = -2.786;
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
float Ro = 10.0;
unsigned long lastSensorRead = 0;
unsigned long lastLcdSwitch = 0;
unsigned long lastHistPush = 0;
const unsigned long SENSOR_INTERVAL = 1000;
const unsigned long LCD_SWITCH_INTERVAL = 2500;
const unsigned long HIST_INTERVAL = 5000; // push to graph history every
5s
bool lcdPageA = true;
float g_temp = 0, g_hum = 0;
float g_ch4 = 0, g_co = 0, g_lpg = 0;
int g_aqi = 0;
String g_status = "GOOD";
// ---------- History buffers for the dashboard graph ----------
#define HISTORY_SIZE 40
float histAqi[HISTORY_SIZE];
float histCh4[HISTORY_SIZE];
float histCo[HISTORY_SIZE];

float histTemp[HISTORY_SIZE];
int histIndex = 0;
int histCount = 0;
void pushHistory() {
histAqi[histIndex] = g_aqi;
histCh4[histIndex] = g_ch4;
histCo[histIndex] = g_co;
histTemp[histIndex] = g_temp;
histIndex = (histIndex + 1) % HISTORY_SIZE;
if (histCount < HISTORY_SIZE) histCount++;
}
// ---------- MQ2 helpers ----------
float readMQ2Voltage() {
int raw = analogRead(MQ2_PIN);
float vAdc = (raw / ADC_MAX) * ADC_VREF;
float vSensor = vAdc / DIVIDER_RATIO;
if (vSensor > MQ2_VCC) vSensor = MQ2_VCC;
return vSensor;
}
float getRs(float vSensor) {
if (vSensor < 0.01) vSensor = 0.01;
return ((MQ2_VCC * RL_VALUE) / vSensor) - RL_VALUE;
}
void calibrateMQ2() {
Serial.println("Calibrating MQ2... keep sensor in clean air.");
lcd.clear();
lcd.setCursor(0, 0);
lcd.print("Calibrating MQ2");
lcd.setCursor(0, 1);
lcd.print("Please wait...");
float rsSum = 0;
const int samples = 50;

for (int i = 0; i < samples; i++) {
float v = readMQ2Voltage();
rsSum += getRs(v);
delay(100);
}
Ro = (rsSum / samples) / RATIO_CLEAN_AIR;
Serial.print("Calibration done. Ro = ");
Serial.println(Ro);
}
float ratioToPpm(float ratio, float a, float b) {
return a * pow(ratio, b);
}
int computeAQI(float ch4, float co, float lpg) {
float score = (co * 4.0) + (ch4 * 0.5) + (lpg * 1.0);
if (score > 500) score = 500;
if (score < 0) score = 0;
return (int)score;
}
String aqiStatus(int aqi) {
if (aqi < 50) return "GOOD";
if (aqi < 100) return "MODERATE";
if (aqi < 150) return "POOR";
if (aqi < 250) return "BAD";
return "HAZARDOUS";
}
// ---------- Web server: JSON data endpoint ----------
void handleData() {
String json = "{";
json += "\"aqi\":" + String(g_aqi) + ",";
json += "\"status\":\"" + g_status + "\",";
json += "\"ch4\":" + String(g_ch4, 1) + ",";
json += "\"co\":" + String(g_co, 1) + ",";
json += "\"lpg\":" + String(g_lpg, 1) + ",";

json += "\"temp\":" + String(g_temp, 1) + ",";
json += "\"hum\":" + String(g_hum, 1) + ",";
json += "\"hist_aqi\":[";
for (int i = 0; i < histCount; i++) {
int idx = (histIndex - histCount + i + HISTORY_SIZE) % HISTORY_SIZE;
json += String(histAqi[idx]);
if (i < histCount - 1) json += ",";
}
json += "],\"hist_ch4\":[";
for (int i = 0; i < histCount; i++) {
int idx = (histIndex - histCount + i + HISTORY_SIZE) % HISTORY_SIZE;
json += String(histCh4[idx], 1);
if (i < histCount - 1) json += ",";
}
json += "],\"hist_co\":[";
for (int i = 0; i < histCount; i++) {
int idx = (histIndex - histCount + i + HISTORY_SIZE) % HISTORY_SIZE;
json += String(histCo[idx], 1);
if (i < histCount - 1) json += ",";
}
json += "],\"hist_temp\":[";
for (int i = 0; i < histCount; i++) {
int idx = (histIndex - histCount + i + HISTORY_SIZE) % HISTORY_SIZE;
json += String(histTemp[idx], 1);
if (i < histCount - 1) json += ",";
}
json += "]}";
server.send(200, "application/json", json);
}
// ---------- Web server: Dashboard page ----------
void handleRoot() {
String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>

<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Room AQI Dashboard</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
body {
background: #ffffff;
color: #222;
font-family: -apple-system, Segoe UI, Roboto, Arial, sans-serif;
margin: 0;
padding: 20px;
}
h1 {
text-align: center;
font-size: 22px;
margin-bottom: 20px;
color: #111;
}
.cards {
display: flex;
flex-wrap: wrap;
gap: 14px;
justify-content: center;
margin-bottom: 24px;
}
.card {
background: #f7f7f9;
border: 1px solid #e4e4e8;
border-radius: 12px;
padding: 16px 20px;
min-width: 130px;
text-align: center;
box-shadow: 0 1px 4px rgba(0,0,0,0.06);
}
.card .label {
font-size: 12px;
color: #777;

text-transform: uppercase;
letter-spacing: 0.05em;
}
.card .value {
font-size: 26px;
font-weight: 600;
margin-top: 6px;
color: #111;
}
.aqi-card .value { font-size: 40px; }
.status-badge {
display: inline-block;
margin-top: 6px;
padding: 4px 12px;
border-radius: 20px;
font-size: 13px;
font-weight: 600;
color: #fff;
}
.chart-container {
max-width: 900px;
margin: 0 auto;
background: #fafafa;
border: 1px solid #e4e4e8;
border-radius: 12px;
padding: 16px;
}
</style>
</head>
<body>
<h1>Room Air Quality Dashboard</h1>
<div class="cards">
<div class="card aqi-card">
<div class="label">AQI</div>
<div class="value" id="aqi">--</div>

<div class="status-badge" id="statusBadge">--</div>
</div>
<div class="card">
<div class="label">CH4 (Methane)</div>
<div class="value" id="ch4">--</div>
</div>
<div class="card">
<div class="label">CO</div>
<div class="value" id="co">--</div>
</div>
<div class="card">
<div class="label">LPG</div>
<div class="value" id="lpg">--</div>
</div>
<div class="card">
<div class="label">Temperature</div>
<div class="value" id="temp">--</div>
</div>
<div class="card">
<div class="label">Humidity</div>
<div class="value" id="hum">--</div>
</div>
</div>
<div class="chart-container">
<canvas id="chart" height="90"></canvas>
</div>
<script>
const ctx = document.getElementById('chart').getContext('2d');
const chart = new Chart(ctx, {
type: 'line',
data: {
labels: [],
datasets: [
{ label: 'AQI', data: [], borderColor: '#e63946', tension: 0.3,
pointRadius: 0 },

{ label: 'CH4 (ppm)', data: [], borderColor: '#457b9d', tension:
0.3, pointRadius: 0 },
{ label: 'CO (ppm)', data: [], borderColor: '#f4a261', tension: 0.3,
pointRadius: 0 }
]
},
options: {
responsive: true,
animation: false,
scales: {
x: { display: true, ticks: { maxTicksLimit: 8 } },
y: { beginAtZero: true }
},
plugins: { legend: { position: 'top' } }
}
});
function statusColor(status) {
if (status === 'GOOD') return '#2a9d8f';
if (status === 'MODERATE') return '#e9c46a';
if (status === 'POOR') return '#f4a261';
if (status === 'BAD') return '#e76f51';
return '#d00000';
}
async function refresh() {
try {
const res = await fetch('/data');
const d = await res.json();
document.getElementById('aqi').textContent = d.aqi;
document.getElementById('ch4').textContent = d.ch4 + ' ppm';
document.getElementById('co').textContent = d.co + ' ppm';
document.getElementById('lpg').textContent = d.lpg + ' ppm';
document.getElementById('temp').textContent = d.temp + ' C';
document.getElementById('hum').textContent = d.hum + ' %';

const badge = document.getElementById('statusBadge');
badge.textContent = d.status;
badge.style.background = statusColor(d.status);
chart.data.labels = d.hist_aqi.map((_, i) => i);
chart.data.datasets[0].data = d.hist_aqi;
chart.data.datasets[1].data = d.hist_ch4;
chart.data.datasets[2].data = d.hist_co;
chart.update();
} catch (e) {
console.log('fetch failed', e);
}
}
refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>
)rawliteral";
server.send(200, "text/html", html);
}
void setup() {
Serial.begin(115200);
delay(500);
pinMode(BUZZER_PIN, OUTPUT);
digitalWrite(BUZZER_PIN, LOW);
analogReadResolution(12);
Wire.begin(SDA_PIN, SCL_PIN);
lcd.init();
lcd.backlight();

dht.begin();
// ---- Connect WiFi ----
lcd.setCursor(0, 0);
lcd.print("Connecting WiFi");
WiFi.begin(WIFI_SSID, WIFI_PASS);
Serial.print("Connecting to WiFi");
int tries = 0;
while (WiFi.status() != WL_CONNECTED && tries < 40) {
delay(500);
Serial.print(".");
tries++;
}
lcd.clear();
if (WiFi.status() == WL_CONNECTED) {
Serial.println();
Serial.print("WiFi connected. IP: ");
Serial.println(WiFi.localIP());
lcd.setCursor(0, 0);
lcd.print("WiFi OK");
lcd.setCursor(0, 1);
lcd.print(WiFi.localIP());
delay(3000);
} else {
Serial.println();
Serial.println("WiFi FAILED - dashboard unavailable, sensors still
work");
lcd.setCursor(0, 0);
lcd.print("WiFi FAILED");
delay(2000);
}
// ---- Warm-up MQ2 ----
lcd.clear();
lcd.setCursor(0, 0);
lcd.print("Warming up MQ2");
Serial.println("Warming up MQ2 sensor (20s)...");

delay(20000);
calibrateMQ2();
lcd.clear();
// ---- Start web server ----
server.on("/", handleRoot);
server.on("/data", handleData);
server.begin();
Serial.println("Web server started.");
}
void loop() {
server.handleClient();
unsigned long now = millis();
if (now - lastSensorRead >= SENSOR_INTERVAL) {
lastSensorRead = now;
float t = dht.readTemperature();
float h = dht.readHumidity();
if (!isnan(t)) g_temp = t;
if (!isnan(h)) g_hum = h;
float vSensor = readMQ2Voltage();
float rs = getRs(vSensor);
float ratio = rs / Ro;
g_lpg = ratioToPpm(ratio, LPG_A, LPG_B);
g_co = ratioToPpm(ratio, CO_A, CO_B);
g_ch4 = ratioToPpm(ratio, CH4_A, CH4_B);
if (g_lpg < 0) g_lpg = 0;
if (g_co < 0) g_co = 0;
if (g_ch4 < 0) g_ch4 = 0;
g_aqi = computeAQI(g_ch4, g_co, g_lpg);
g_status = aqiStatus(g_aqi);

digitalWrite(BUZZER_PIN, g_aqi >= 150 ? HIGH : LOW);
Serial.println("---------------------------");
Serial.print("AQI: "); Serial.print(g_aqi);
Serial.print(" Status: "); Serial.println(g_status);
Serial.print("CH4: "); Serial.print(g_ch4, 1); Serial.println(" ppm");
Serial.print("CO : "); Serial.print(g_co, 1); Serial.println(" ppm");
Serial.print("LPG: "); Serial.print(g_lpg, 1); Serial.println(" ppm");
Serial.print("Temp: "); Serial.print(g_temp, 1); Serial.print(" C ");
Serial.print("Hum: "); Serial.print(g_hum, 1); Serial.println(" %");
}
if (now - lastHistPush >= HIST_INTERVAL) {
lastHistPush = now;
pushHistory();
}
if (now - lastLcdSwitch >= LCD_SWITCH_INTERVAL) {
lastLcdSwitch = now;
lcdPageA = !lcdPageA;
lcd.clear();
if (lcdPageA) {
lcd.setCursor(0, 0);
lcd.print("AQI:"); lcd.print(g_aqi); lcd.print(" ");
lcd.print(g_status);
lcd.setCursor(0, 1);
lcd.print("T:"); lcd.print(g_temp, 1); lcd.print("C H:");
lcd.print(g_hum, 0); lcd.print("%");
} else {
lcd.setCursor(0, 0);
lcd.print("CH4:"); lcd.print(g_ch4, 0); lcd.print("ppm");
lcd.setCursor(0, 1);
lcd.print("CO:"); lcd.print(g_co, 0); lcd.print("ppm");
}
}
}
