#include <WiFi.h>

The Pointing laser — page 23

#include <esp_now.h>
#include <esp_wifi.h>
#include <ESP32Servo.h>
#define PAN_PIN 5 // bottom servo, driven by roll
#define TILT_PIN 4 // top servo, driven by pitch
#define ESPNOW_CHANNEL 1 // must match the transmitter
// How much sensor tilt gives full servo sweep. Smaller = twitchier.
const float INPUT_RANGE_DEG = 60.0f;
// Servo travel limits. Narrow these if your bracket binds -- a stalled
// 9 g servo gets hot and eats current.
const float PAN_MIN = 10.0f, PAN_MAX = 170.0f;
const float TILT_MIN = 20.0f, TILT_MAX = 160.0f;
// Flip these if a servo moves the wrong way.
const bool PAN_INVERT = false;
const bool TILT_INVERT = false;
const float DEADZONE_DEG = 2.0f; // ignore wobble this small
const float SMOOTHING = 0.20f; // lower = smoother and laggier
const uint32_t SIGNAL_TIMEOUT_MS = 500; // no packets for this long -> centre
const uint32_t UPDATE_INTERVAL_MS = 20; // 50 Hz servo updates
const uint32_t PRINT_INTERVAL_MS = 200;
// true = however you hold the sensor at power-up becomes centre
// false = use the sensor's own absolute zero
#define ZERO_ON_FIRST_PACKET true
const int SERVO_MIN_US = 500;
const int SERVO_MAX_US = 2400;
// Byte-for-byte identical to the transmitter's struct.
typedef struct __attribute__((packed)) {
uint8_t magic;
uint32_t seq;
float roll;
float pitch;
float yaw;
uint8_t accuracy;
} OrientationPacket;
Servo panServo, tiltServo;
// Shared between the ESP-NOW callback and loop(). The callback runs in the
// WiFi task, so touch these only inside a critical section.
volatile float rxRoll = 0, rxPitch = 0;
volatile uint32_t rxSeq = 0, lastPacketMs = 0;
volatile uint32_t packetCount = 0, droppedCount = 0, badCount = 0;
portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;
float panCurrent = 90.0f, tiltCurrent = 90.0f;
float rollOffset = 0, pitchOffset = 0;
bool haveOffset = false;
uint32_t lastUpdate = 0, lastPrint = 0;
// Keep this callback SHORT -- copy the data out and leave.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
#endif
if (len != sizeof(OrientationPacket)) { badCount++; return; }
OrientationPacket p;
memcpy(&p, data, sizeof(p));
if (p.magic != 0xA7) { badCount++; return; } // not one of ours
portENTER_CRITICAL_ISR(&dataMux);
if (packetCount > 0 && p.seq > rxSeq + 1) droppedCount += (p.seq - rxSeq - 1);
rxRoll = p.roll;
rxPitch = p.pitch;

rxSeq = p.seq;
lastPacketMs = millis();
packetCount++;
portEXIT_CRITICAL_ISR(&dataMux);
}
float clampf(float v, float lo, float hi) {
return v < lo ? lo : (v > hi ? hi : v);
}
// Map a sensor angle onto a servo angle.
float mapAngle(float in, float lo, float hi, bool invert) {
if (fabsf(in) < DEADZONE_DEG) in = 0.0f; // deadzone
else in -= (in > 0 ? DEADZONE_DEG : -DEADZONE_DEG);
float frac = clampf(in / INPUT_RANGE_DEG, -1.0f, 1.0f); // -1 .. +1
if (invert) frac = -frac;
float mid = (lo + hi) * 0.5f;
float half = (hi - lo) * 0.5f;
return mid + frac * half;
}
void writeAngle(Servo &s, float deg) {
deg = clampf(deg, 0.0f, 180.0f);
int us = SERVO_MIN_US + (int)((deg / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US));
s.writeMicroseconds(us);
}
void setup() {
Serial.begin(115200);
delay(500);
Serial.println("\n=== ESP-NOW -> pan/tilt receiver ===");
ESP32PWM::allocateTimer(0);
ESP32PWM::allocateTimer(1);
ESP32PWM::allocateTimer(2);
ESP32PWM::allocateTimer(3);
panServo.setPeriodHertz(50);
tiltServo.setPeriodHertz(50);
int panCh = panServo.attach(PAN_PIN, SERVO_MIN_US, SERVO_MAX_US);
int tiltCh = tiltServo.attach(TILT_PIN, SERVO_MIN_US, SERVO_MAX_US);
Serial.printf("pan (GPIO %d) -> ch %d, tilt (GPIO %d) -> ch %d\n",
PAN_PIN, panCh, TILT_PIN, tiltCh);
if (panCh == tiltCh) Serial.println("!! both servos share a channel");
writeAngle(panServo, panCurrent);
writeAngle(tiltServo, tiltCurrent);
Serial.println("Servos centred.");
WiFi.mode(WIFI_STA);
WiFi.disconnect();
esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
Serial.print(">>> PUT THIS MAC IN THE TRANSMITTER: ");
Serial.println(WiFi.macAddress());
if (esp_now_init() != ESP_OK) {
Serial.println("!! esp_now_init failed");
while (1) delay(100);
}
esp_now_register_recv_cb(onDataRecv);
Serial.printf("Listening on channel %d.\n\n", ESPNOW_CHANNEL);
}
void loop() {
uint32_t now = millis();
if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
lastUpdate = now;
// Snapshot the shared values quickly, then work with the copies.
float roll, pitch;
uint32_t lastSeen, count, dropped;
portENTER_CRITICAL(&dataMux);
roll = rxRoll;
pitch = rxPitch;
lastSeen = lastPacketMs;
count = packetCount;
dropped = droppedCount;
portEXIT_CRITICAL(&dataMux);
bool live = (count > 0) && (now - lastSeen < SIGNAL_TIMEOUT_MS);
float panTarget, tiltTarget;
if (live) {
#if ZERO_ON_FIRST_PACKET
if (!haveOffset) {
rollOffset = roll;
pitchOffset = pitch;
haveOffset = true;
Serial.printf("Neutral captured: roll %.1f pitch %.1f\n",
rollOffset, pitchOffset);
}
roll -= rollOffset;
pitch -= pitchOffset;
#endif
panTarget = mapAngle(roll, PAN_MIN, PAN_MAX, PAN_INVERT);
tiltTarget = mapAngle(pitch, TILT_MIN, TILT_MAX, TILT_INVERT);
} else {
// Signal lost -- glide to centre rather than freezing at an angle.
panTarget = (PAN_MIN + PAN_MAX) * 0.5f;
tiltTarget = (TILT_MIN + TILT_MAX) * 0.5f;
}
panCurrent += SMOOTHING * (panTarget - panCurrent);
tiltCurrent += SMOOTHING * (tiltTarget - tiltCurrent);
writeAngle(panServo, panCurrent);
writeAngle(tiltServo, tiltCurrent);
if (now - lastPrint >= PRINT_INTERVAL_MS) {
lastPrint = now;
if (live) {
Serial.printf("roll %7.1f -> pan %6.1f | pitch %7.1f -> tilt %6.1f | rx %lu drop %lu\n",
roll, panCurrent, pitch, tiltCurrent, count, dropped);
} else {
Serial.printf("no signal (%lu packets so far) -- centring\n", count);
}
}
}
