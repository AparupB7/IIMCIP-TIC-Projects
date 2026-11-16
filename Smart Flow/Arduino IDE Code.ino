#define BLYNK_TEMPLATE_ID "TMPL3RZRBeDYU"
#define BLYNK_TEMPLATE_NAME "Flow Meter"
#define BLYNK_AUTH_TOKEN "oEPjIiM-Rwuz-VSRTNlaHddsPbkjNHXm"
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <EEPROM.h>
// Wi-Fi credentials
const char ssid[] = "your-wifi-name";
const char pass[] = "wifi-password";
// Water Flow Sensor pin
#define FLOW_SENSOR_PIN 4 // Adjust according to your wiring
BlynkTimer timer;
volatile unsigned long pulseCount = 0;
float flowRate = 0.0; // L/min
float totalLiters = 0.0;
unsigned long lastMillis = 0;
// ISR for water flow sensor
void IRAM_ATTR pulseCounter() {
pulseCount++;
}
void setup() {
Serial.begin(115200);
delay(100);
// Connect to Blynk
Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
// Setup water flow sensor pin
pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter,
RISING);
// EEPROM for total water usage storage
EEPROM.begin(32);
EEPROM.get(0, totalLiters);
if (isnan(totalLiters)) totalLiters = 0.0;
lastMillis = millis();
// Send data every 5 seconds
timer.setInterval(5000L, sendFlowDataToBlynk);
}

IoT Flow Meter

Now verify the code on Arduino
Then upload the code into the ESP32.

Tasks
The Flow Rate value in the Serial Monitor should read zero. If the Water Flowed value
isn’t initially zero, press the reset button on the ESP32. Now slowly blow into the Flow
Meter pipe and check whether the flowrate and water flowed variables change in the
Serial Monitor. Go to the blynk Dashboard page - the meter gauges should reflect the
change in values.

IoT Flow Meter
void loop() {
Blynk.run();
timer.run();
}
void sendFlowDataToBlynk() {
unsigned long currentMillis = millis();
float intervalSec = (currentMillis - lastMillis) / 1000.0;
lastMillis = currentMillis;
// Calculate flow rate (assuming 450 pulses per liter, adjust for your
sensor)
flowRate = (pulseCount / 450.0) / (intervalSec / 60.0); // L/min
totalLiters += (pulseCount / 450.0);
// Serial output
Serial.printf("Flow Rate: %.2f L/min | Total Liters: %.2f\n", flowRate,
totalLiters);
// Send to Blynk
Blynk.virtualWrite(V0, flowRate);
Blynk.virtualWrite(V1, totalLiters);
// Reset pulse count
pulseCount = 0;
// Save total liters to EEPROM
EEPROM.put(0, totalLiters);
EEPROM.commit();
}
