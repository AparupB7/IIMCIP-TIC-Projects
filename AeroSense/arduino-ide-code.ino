#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define RELAY_PIN 18
#define MQ2_PIN 2
#define OLED_SDA 6
#define OLED_SCL 7
#define OLED_ADDR 0x3C
#define ACTIVE_LOW true // set false if your relay board is
active-HIGH
const int ON_DELTA = 20; // fan ON when reading rises this far above
baseline (mV)
const int OFF_DELTA = 15; // fan OFF when it falls below this
const uint32_t MIN_SWITCH_MS = 50000;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
int baseline = 0;
bool fanOn = false;
uint32_t lastSwitch = 0;
void setFan(bool on) {
digitalWrite(RELAY_PIN, (on ^ ACTIVE_LOW) ? HIGH : LOW);
fanOn = on;
lastSwitch = millis();
}
int readMQ2() {
long sum = 0;
for (int i = 0; i < 20; i++) { sum += analogReadMilliVolts(MQ2_PIN);
delay(5); }
return sum / 20;
}
void showMessage(const char *l1, const char *l2 = "") {
display.clearDisplay();
display.setTextSize(1);
display.setTextColor(SSD1306_WHITE);
display.setCursor(0, 0);
display.println("AIR PURIFIER");
display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
display.setCursor(0, 20); display.println(l1);
display.setCursor(0, 34); display.println(l2);
display.display();
}
void showStatus(int v, int delta) {
const char *quality = delta < OFF_DELTA ? "GOOD" :
delta < ON_DELTA ? "MODERATE" : "POOR";
display.clearDisplay();
display.setTextSize(1);
display.setTextColor(SSD1306_WHITE);
display.setCursor(0, 0);
display.print("AIR PURIFIER");
display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
display.setCursor(0, 14); display.printf("Air: %s", quality);
display.setCursor(0, 25); display.printf("MQ2: %d mV (+%d)", v,
delta);
int w = constrain(map(delta, 0, 300, 0, 124), 0, 124);
display.drawRect(0, 37, 126, 8, SSD1306_WHITE);
display.fillRect(1, 38, w, 6, SSD1306_WHITE);
display.setTextSize(2);
display.setCursor(0, 48);
display.print(fanOn ? "FAN: ON" : "FAN: OFF");
display.display();
}
void setup() {
Serial.begin(115200);
pinMode(RELAY_PIN, OUTPUT);
setFan(false);
analogSetPinAttenuation(MQ2_PIN, ADC_11db);
Wire.begin(OLED_SDA, OLED_SCL);
if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
Serial.println("OLED not found! Check wiring/address.");
}
for (int s = 60; s > 0; s--) {
char buf[24];
snprintf(buf, sizeof(buf), "Warming up: %d s", s);
showMessage(buf, "Keep in clean air");
delay(1000);
}
showMessage("Calibrating...", "Setting baseline");
long sum = 0;
for (int i = 0; i < 20; i++) { sum += readMQ2(); delay(500); }
baseline = sum / 20;
Serial.printf("Baseline: %d mV\n", baseline);
}
void loop() {
int v = readMQ2();
int delta = v - baseline;

Serial.printf("MQ2: %d mV delta: %d fan: %s\n", v, delta, fanOn ? "ON" : "OFF");
if (millis() - lastSwitch > MIN_SWITCH_MS) {
if (!fanOn && delta > ON_DELTA) setFan(true);
if (fanOn && delta < OFF_DELTA) setFan(false);
}
showStatus(v, delta);
delay(500);
}
