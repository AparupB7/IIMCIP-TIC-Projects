#define BLYNK_TEMPLATE_ID "your_template_id"
#define BLYNK_TEMPLATE_NAME "your_template_name"
#define BLYNK_AUTH_TOKEN "your_template_auth_token"
//----------------------LIBRARIES-----------------------//
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
//----------------------WiFi----------------------------//
char ssid[] = "your_wifi_name";
char pass[] = "your_wifi_password";
//----------------------LCD-----------------------------//
LiquidCrystal_I2C lcd(0x27, 16, 2);
//----------------------PINS----------------------------//
#define MQ2_PIN 2 // Analog pin (ADC1, safe on C6)
#define BUZZER_PIN 18
#define WHITE_LED 20
#define RED_LED 21

#define SDA_PIN 22
#define SCL_PIN 23
//----------------------VARIABLES-----------------------//
int gasValue = 0;

int lpgPPM = 0;
int methanePPM = 0;
int hydrogenPPM = 0;
bool buzzerState = false;
unsigned long lastBeepTime = 0;
bool whiteLedState = false;
unsigned long lastWhiteBlink = 0;
String statusText = "SAFE";
//Raw ADC thresholds
const int SAFE_LEVEL = 1000;
const int WARNING_LEVEL = 1200;
//------------------------------------------------------//
BlynkTimer timer;
//======================================================//
// READ GAS SENSOR
//======================================================//
void readGasSensor()
{
gasValue = analogRead(MQ2_PIN);
//Estimated values (Demo Purpose)
lpgPPM = map(gasValue,0,4095,0,1000);
methanePPM = map(gasValue,0,4095,0,900);
hydrogenPPM = map(gasValue,0,4095,0,1200);
lpgPPM = constrain(lpgPPM,0,1000);
methanePPM = constrain(methanePPM,0,900);
hydrogenPPM = constrain(hydrogenPPM,0,1200);
//----------------SAFE----------------//
if(gasValue < SAFE_LEVEL)
{

statusText = "SAFE";
if (millis() - lastWhiteBlink >= 500)
{
lastWhiteBlink = millis();
whiteLedState = !whiteLedState;
digitalWrite(WHITE_LED, whiteLedState);
}
digitalWrite(RED_LED,LOW);
digitalWrite(BUZZER_PIN,LOW);
}
//----------------WARNING----------------//
else if(gasValue >= SAFE_LEVEL && gasValue < WARNING_LEVEL)
{
statusText = "WARNING";
digitalWrite(WHITE_LED,LOW);
digitalWrite(RED_LED,HIGH);
if (millis() - lastBeepTime >= 500) // Change 500 to adjust beep
speed
{
lastBeepTime = millis();
buzzerState = !buzzerState;
digitalWrite(BUZZER_PIN, buzzerState);
}
}
//----------------LEAK----------------//

else
{
statusText = "GAS LEAK!";
digitalWrite(WHITE_LED,LOW);
digitalWrite(RED_LED,HIGH);
if (millis() - lastBeepTime >= 500) // Change 500 to adjust beep
speed
{
lastBeepTime = millis();
buzzerState = !buzzerState;
digitalWrite(BUZZER_PIN, buzzerState);
}
}
//----------------------------------------------------//
// SERIAL DEBUG (this was completely missing before)
//----------------------------------------------------//
Serial.print("Gas Raw: ");
Serial.print(gasValue);
Serial.print(" | LPG: ");
Serial.print(lpgPPM);
Serial.print(" | CH4: ");
Serial.print(methanePPM);
Serial.print(" | H2: ");
Serial.print(hydrogenPPM);
Serial.print(" | Status: ");
Serial.println(statusText);
//----------------------------------------------------//
// LCD
//----------------------------------------------------//
lcd.setCursor(0,0);

lcd.print("Gas: ");
lcd.print(" ");
lcd.setCursor(5,0);
lcd.print(gasValue);
lcd.setCursor(0,1);
lcd.print(" ");
lcd.setCursor(0,1);
lcd.print(statusText);
//----------------------------------------------------//
// BLYNK
//----------------------------------------------------//
Blynk.virtualWrite(V0, lpgPPM);
Blynk.virtualWrite(V1, methanePPM);
Blynk.virtualWrite(V2, hydrogenPPM);
}
//======================================================//
// SETUP
//======================================================//
void setup()
{
Serial.begin(115200);
delay(200); // FIX: small settle time before first prints
pinMode(BUZZER_PIN, OUTPUT);
pinMode(WHITE_LED, OUTPUT);
pinMode(RED_LED, OUTPUT);
digitalWrite(BUZZER_PIN, LOW);
digitalWrite(WHITE_LED, LOW);
digitalWrite(RED_LED, LOW);
//ADC Resolution

analogReadResolution(12);
//----------------LCD----------------//
Wire.begin(SDA_PIN, SCL_PIN);
lcd.init();
lcd.backlight();
lcd.setCursor(0,0);
lcd.print("Gas Leakage");
lcd.setCursor(0,1);
lcd.print("Connecting...");
//----------------WiFi----------------//
WiFi.begin(ssid, pass);
while (WiFi.status() != WL_CONNECTED)
{
delay(500);
Serial.print(".");
}
Serial.println();
Serial.println("WiFi Connected");
WiFi.setSleep(false);
//----------------Blynk--------------//
Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
lcd.clear();
lcd.setCursor(0,0);
lcd.print("WiFi Connected");

delay(1500);
timer.setInterval(1000L, readGasSensor);
}
void loop()
{
Blynk.run();
timer.run();
}
