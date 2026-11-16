#define PIR_PIN 4
#define RELAY_PIN 18
void setup() {
Serial.begin(115200);
// Initialize PIR sensor and relay
pinMode(PIR_PIN, INPUT);
pinMode(RELAY_PIN, OUTPUT);
// Relay OFF initially (Active-LOW relay)
digitalWrite(RELAY_PIN, HIGH);
Serial.println("--------------------------------");
Serial.println("LumiSense Motion Based Lighting System");
Serial.println("--------------------------------");
Serial.println("PIR Sensor Initializing...");
// Allow PIR sensor to stabilize
delay(30000);
Serial.println("System Ready!");
}
void loop() {

// Read PIR sensor
int motion = digitalRead(PIR_PIN);
if (motion == HIGH) {
// Motion detected → Bulb ON
digitalWrite(RELAY_PIN, LOW);
Serial.println("Motion Detected -> Bulb ON");
}
else {
// No motion → Bulb OFF
digitalWrite(RELAY_PIN, HIGH);
Serial.println("No Motion -> Bulb OFF");
}
delay(500);
}
