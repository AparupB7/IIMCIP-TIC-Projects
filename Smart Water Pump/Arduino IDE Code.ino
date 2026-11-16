#define BLYNK_TEMPLATE_ID "Your-Template-ID"
#define BLYNK_TEMPLATE_NAME "Smart Water-Pump"
#define BLYNK_AUTH_TOKEN "your auth token"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
char ssid[] = "your-wifi-name";
char pass[] = "your-wifi-password";
// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C // change to 0x3D if screen stays blank
#define SDA_PIN 5
#define SCL_PIN 6
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire,
-1);
// ---------------- Relay ----------------
#define RELAY_PIN 7 // changed from GPIO10 -> GPIO7 (board didn't
expose GPIO10)
// ---------------- NTP ----------------
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; // IST = UTC+5:30. Change for your
timezone.
const int daylightOffset_sec = 0;
BlynkTimer timer;
// ---------------- Blynk Datastream Map ----------------
Arduino IDE Code -

// Schedule variables
int pumpOnHour = -1;
int pumpOnMinute = -1;
int pumpOffHour = -1;
int pumpOffMinute = -1;
bool pumpRunning = false;
// ----------------------------------------------------
// Blynk Sync
// ----------------------------------------------------
BLYNK_CONNECTED()
{
Blynk.syncVirtual(V0);
Blynk.syncVirtual(V1);
Blynk.syncVirtual(V2);
Blynk.syncVirtual(V3);
}

// ----------------------------------------------------
// Receive ON Time
// ----------------------------------------------------
BLYNK_WRITE(V0)
{
pumpOnHour = param.asInt();
Serial.print("ON Hour : ");
Serial.println(pumpOnHour);
}

BLYNK_WRITE(V1)
{
pumpOnMinute = param.asInt();
Serial.print("ON Minute : ");
Serial.println(pumpOnMinute);
}

BLYNK_WRITE(V2)
{
pumpOffHour = param.asInt();
Serial.print("OFF Hour : ");
Serial.println(pumpOffHour);
}
BLYNK_WRITE(V3)
{
pumpOffMinute = param.asInt();
Serial.print("OFF Minute : ");
Serial.println(pumpOffMinute);
}
// ----------------------------------------------------
// Clock + Pump Logic (runs every second via BlynkTimer)
// ----------------------------------------------------
void updateClock()
{
struct tm timeinfo;
if (!getLocalTime(&timeinfo)) {
Serial.println("Failed to obtain time");
display.clearDisplay();
display.setTextSize(1);
display.setCursor(0, 0);
display.println("Time sync failed");
display.display();
return;
}
char timeBuffer[6];
sprintf(timeBuffer,

"%02d:%02d"

, timeinfo.tm_hour, timeinfo.tm_min);

// ---- OLED Display ----
display.clearDisplay();
display.setTextSize(3);
display.setTextColor(SSD1306_WHITE);
display.setCursor(10, 10);
display.println(timeBuffer);

display.setTextSize(1);
display.setCursor(10, 50);
display.print("PUMP: ");
display.println(pumpRunning ? "ON" : "OFF");
display.display();
// ---- Blynk Time Display ----
Blynk.virtualWrite(V4, timeBuffer);
Serial.print("Current Time : ");
Serial.println(timeBuffer);
if (pumpOnHour >= 0 &&
pumpOnMinute >= 0 &&
timeinfo.tm_hour == pumpOnHour &&
timeinfo.tm_min == pumpOnMinute &&
!pumpRunning)
{
digitalWrite(RELAY_PIN, LOW); // active-LOW relay: LOW = energized = ON
pumpRunning = true;
Blynk.virtualWrite(V5,

"PUMP ON");

Serial.println("PUMP ON");
}

if (pumpOffHour >= 0 &&
pumpOffMinute >= 0 &&
timeinfo.tm_hour == pumpOffHour &&
timeinfo.tm_min == pumpOffMinute &&
pumpRunning)
{
digitalWrite(RELAY_PIN, HIGH); // HIGH = de-energized = OFF
pumpRunning = false;
Blynk.virtualWrite(V5,

"PUMP OFF");

Serial.println("PUMP OFF");
}
}
void setup()
{
Serial.begin(115200);
// I2C bus for OLED (no RTC anymore, so OLED gets it alone)
Wire.begin(SDA_PIN, SCL_PIN);
if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
Serial.println("OLED not found");
while (1);
}
display.clearDisplay();
display.setTextColor(SSD1306_WHITE);
display.setTextSize(1);
display.setCursor(0, 0);
display.println("Connecting WiFi...
");

display.display();
// Relay — start OFF
pinMode(RELAY_PIN, OUTPUT);
digitalWrite(RELAY_PIN, HIGH); // assumes active-LOW relay module
(most common)
// WiFi + Blynk (this call blocks until WiFi connects)
Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
// NTP time sync — needs WiFi, which is already up after Blynk.begin()
configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

display.clearDisplay();
display.setCursor(0, 0);
display.println("WiFi Connected");
display.println("Syncing time...
");

display.display();
timer.setInterval(1000L, updateClock);
}
void loop()
{
Blynk.run();
timer.run();
}
