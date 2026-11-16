#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <LiquidCrystal_I2C.h>
// WIFI DETAILS
const char* ssid = "wifi-name";
const char* password = "wifi-password";
// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);
// MPU6050
Adafruit_MPU6050 mpu;
// PREVIOUS VALUES
float prevX = 0;
float prevY = 0;
float prevZ = 0;
// FILTERED MAGNITUDE
float filteredMagnitude = 0;

void setup() {
Serial.begin(115200);
// I2C PINS
Wire.begin(6, 7);
// LCD START
lcd.init();
lcd.backlight();
lcd.setCursor(0, 0);
lcd.print("Connecting WiFi");
// WIFI CONNECT
WiFi.begin(ssid, password);
while (WiFi.status() != WL_CONNECTED) {
delay(500);
lcd.print(".");
}
lcd.clear();
lcd.setCursor(0, 0);
lcd.print("WiFi Connected");
delay(1500);
// INTERNET TIME
configTime(19800, 0, "pool.ntp.org");
// MPU START
if (!mpu.begin()) {
lcd.clear();
lcd.setCursor(0, 0);
lcd.print("MPU ERROR");

while (1);
}
// BETTER SENSITIVITY
mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
lcd.clear();
lcd.setCursor(0, 0);
lcd.print("System Ready");
delay(1500);
}
void loop() {
// GET REAL TIME
struct tm timeinfo;
getLocalTime(&timeinfo);
// READ SENSOR
sensors_event_t a, g, temp;
mpu.getEvent(&a, &g, &temp);
// CURRENT VALUES
float x = a.acceleration.x;
float y = a.acceleration.y;
float z = a.acceleration.z;
// DIFFERENCE VALUES
float deltaX = abs(x - prevX);
float deltaY = abs(y - prevY);
float deltaZ = abs(z - prevZ);
// SAVE CURRENT VALUES
prevX = x;
prevY = y;
prevZ = z;

// TOTAL VIBRATION
float vibration = deltaX + deltaY + deltaZ;
// BETTER EARTHQUAKE SCALE
float rawMagnitude = vibration / 3.5;
// LIMIT VALUES
if (rawMagnitude > 9.5) {
rawMagnitude = 9.5;
}
// SMOOTHING FILTER
filteredMagnitude =
(filteredMagnitude * 0.7) + (rawMagnitude * 0.3);
// VERY SMALL MOVEMENT REMOVAL
if (filteredMagnitude < 0.8) {
filteredMagnitude = 0.0;
}
// LCD DISPLAY
lcd.clear();
// SHOW REAL TIME
lcd.setCursor(0, 0);
if (timeinfo.tm_hour < 10) lcd.print("0");
lcd.print(timeinfo.tm_hour);
lcd.print(":");
if (timeinfo.tm_min < 10) lcd.print("0");
lcd.print(timeinfo.tm_min);
lcd.print(":");
if (timeinfo.tm_sec < 10) lcd.print("0");
lcd.print(timeinfo.tm_sec);

// SAFE
if (filteredMagnitude < 2.5) {
lcd.setCursor(0, 1);
lcd.print("SAFE ");
lcd.print(filteredMagnitude, 1);
}
// MILD TREMOR
else if (filteredMagnitude >= 2.5 &&
filteredMagnitude < 6.0) {
lcd.setCursor(0, 1);
lcd.print("MILD ");
lcd.print(filteredMagnitude, 1);
}
// DANGER
else {
lcd.setCursor(0, 1);
lcd.print("DANGER ");
lcd.print(filteredMagnitude, 1);
}
// SERIAL DEBUG
Serial.print("Vibration: ");
Serial.print(vibration);
Serial.print(" Magnitude: ");
Serial.println(filteredMagnitude);
delay(150);
}
