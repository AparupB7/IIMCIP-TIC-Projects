#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
//========================
// Pin Definitions
//========================
#define DHTPIN 4
#define DHTTYPE DHT11
#define RELAY_PIN 18
#define SDA_PIN 8
#define SCL_PIN 9
//========================
// Temperature Threshold
//========================
const float TEMP_THRESHOLD = 35.0;
//========================
// Objects
//========================
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

//========================
// Setup
//========================
void setup()
{
Serial.begin(115200);
Wire.begin(SDA_PIN, SCL_PIN);
dht.begin();
lcd.init();
lcd.backlight();
pinMode(RELAY_PIN, OUTPUT);
// Relay OFF (Active LOW Relay)
digitalWrite(RELAY_PIN, HIGH);
lcd.clear();
lcd.setCursor(0, 0);
lcd.print("ThermoFlow");
lcd.setCursor(0, 1);
lcd.print("Initializing");
delay(2000);
lcd.clear();
}
//========================
// Main Loop
//========================
void loop()
{
float temperature = dht.readTemperature();
if (isnan(temperature))

{
lcd.clear();
lcd.setCursor(0,0);
lcd.print("Sensor Error");
Serial.println("DHT11 Error");
delay(2000);
return;
}
bool fanState = (temperature >= TEMP_THRESHOLD);
// Relay Control
if (fanState)
{
digitalWrite(RELAY_PIN, LOW); // Relay ON (Active LOW)
}
else
{
digitalWrite(RELAY_PIN, HIGH); // Relay OFF
}
// LCD Display
lcd.clear();
lcd.setCursor(0,0);
lcd.print("Temp: ");
lcd.print(temperature,1);
lcd.print((char)223);
lcd.print("C");
lcd.setCursor(0,1);
if(fanState)
lcd.print("Fan Status: ON");
else
lcd.print("Fan Status:OFF");

// Serial Monitor
Serial.print("Temperature : ");
Serial.print(temperature);
Serial.print(" C");
if(fanState)
Serial.println(" | FAN ON");
else
Serial.println(" | FAN OFF");
delay(2000);
}
