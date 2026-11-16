#define BLYNK_TEMPLATE_ID "YourTemplateID" // <-- from Blynk.Console
#define BLYNK_TEMPLATE_NAME "SoilSync" // <-- from Blynk.Console
#define BLYNK_AUTH_TOKEN "YourAuthToken" // <-- from Blynk.Console
#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <ESP32Servo.h>
// -------------------- WiFi Credentials --------------------
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";
// -------------------- DHT11 --------------------
#define DHTPIN 2 // DATA -> GPIO2
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
// -------------------- Soil Moisture (FC-28) --------------------
#define SOIL_PIN 0 // AO -> GPIO0 (ADC1)
// -------------------- Servo --------------------
#define SERVO_PIN 3 // Signal -> GPIO3
Servo myServo;
// -------------------- Active Buzzer --------------------
#define BUZZER_PIN 4 // Positive -> GPIO4
// -------------------- Thresholds --------------------
int moistureThreshold = 2500; // Adjust after calibration
float tempThreshold = 30.0;
// -------------------- Blynk Virtual Pins --------------------
// V0 - Temperature

// V1 - Humidity
// V2 - Soil Moisture value
// V3 - Irrigation status (0/1)
// V4 - Manual override switch (optional, from dashboard)
BlynkTimer timer;
bool manualOverride = false;
bool manualState = false;
// Called whenever the manual switch widget (V4) is toggled on the
dashboard
BLYNK_WRITE(V4)
{
manualOverride = true;
manualState = param.asInt(); // 1 = force ON, 0 = force OFF
}
void sendSensorData()
{
int soilValue = analogRead(SOIL_PIN);
float temperature = dht.readTemperature();
float humidity = dht.readHumidity();
if (isnan(temperature) || isnan(humidity))
{
Serial.println("DHT Error");
return;
}
bool irrigation = false;
if (manualOverride)
{
irrigation = manualState;
}
else if (soilValue > moistureThreshold && temperature >= tempThreshold)
{
irrigation = true;

}
//---------------- Serial Monitor ----------------
Serial.print("Temperature : ");
Serial.print(temperature);
Serial.print(" Humidity : ");
Serial.print(humidity);
Serial.print(" Soil : ");
Serial.print(soilValue);
Serial.print(" Irrigation : ");
Serial.println(irrigation ? "ON" : "OFF");
//---------------- Blynk Dashboard Update ----------------
Blynk.virtualWrite(V0, temperature);
Blynk.virtualWrite(V1, humidity);
Blynk.virtualWrite(V2, soilValue);
Blynk.virtualWrite(V3, irrigation ? 1 : 0);
//---------------- Buzzer ----------------
digitalWrite(BUZZER_PIN, irrigation ? HIGH : LOW);
//---------------- Servo ----------------
if (irrigation)
{
myServo.write(0);
delay(700);
myServo.write(90);
delay(700);
myServo.write(180);
delay(700);
myServo.write(90);
delay(700);
}
else
{
myServo.write(90);
delay(200);

}
}
void setup()
{
Serial.begin(115200);
dht.begin();
myServo.attach(SERVO_PIN);
myServo.write(90); // Servo at rest position
pinMode(BUZZER_PIN, OUTPUT);
digitalWrite(BUZZER_PIN, LOW);
Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
// Read sensors & push to dashboard every 2 seconds
timer.setInterval(2000L, sendSensorData);
}
void loop()
{
Blynk.run();
timer.run();
}
