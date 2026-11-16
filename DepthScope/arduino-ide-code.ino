/*
 * VL53L5CX ToF Sensor + BNO08X IMU Reader for ESP32
 * Web Serial edition — v0.2.0
 *
 * Streams 8x8 (or 4x4) distance data and IMU orientation as newline-delimited
 * JSON, and accepts single-line commands so the browser viewer can identify the
 * device and change settings at runtime.
 *
 * Wiring (both sensors share the I2C bus):
 *   VIN -> 3V3
 *   GND -> GND
 *   SDA -> GPIO 21
 *   SCL -> GPIO 22
 *   LPn -> GPIO 19 (VL53L5CX enable, driven HIGH)
 *
 * ---------------------------------------------------------------------------
 * What changed from v0.1.0, and why
 * ---------------------------------------------------------------------------
 * 1. Baud rate 115200 -> 921600.
 *    An 8x8 frame is ~700 bytes of JSON. At 15 Hz that is ~105 kbps of payload,
 *    which does not fit inside 115200 baud once start/stop bits are counted.
 *    The old firmware was silently backing up in the TX buffer; in a browser
 *    that shows up as growing latency. 921600 leaves ~8x headroom.
 *
 * 2. One buffered Serial.write() per frame instead of ~130 Serial.print() calls.
 *    Each print() has fixed overhead; batching cut frame assembly time a lot and
 *    stops partial frames from interleaving with command replies.
 *
 * 3. Command interface (see COMMANDS below).
 *    Web Serial cannot tell you which port is the ESP32 — the browser only sees
 *    a VID/PID. The viewer sends "?" after opening and waits for the id reply,
 *    so it can confirm you picked the right port and the right baud rate.
 *
 * 4. Init failure no longer traps in while(1).
 *    It reports {"error":...} every 2 s and still answers commands, so the
 *    browser can show a real reason instead of an empty screen.
 *
 * 5. Frames carry "seq", "t" and "res" so the viewer can measure true frame
 *    rate, detect dropped frames, and rebuild its geometry when you switch
 *    between 4x4 and 8x8 at runtime.
 *
 * ---------------------------------------------------------------------------
 * COMMANDS (send one per line, terminated with \n)
 * ---------------------------------------------------------------------------
 *   ?        Identify. Replies with {"id":"vl53l5cx","v":...,"res":...,...}
 *   s1 / s0  Start / stop streaming frames
 *   r4 / r8  Set resolution to 4x4 or 8x8 (frequency is clamped to fit)
 *   f<hz>    Set ranging frequency, e.g. f15  (max 15 at 8x8, 60 at 4x4)
 *   t        Tare the IMU: current orientation becomes identity
 *   T        Clear the tare
 *   x1 / x0  Include / exclude per-zone reflectance in frames
 *   R        Reboot the ESP32
 */

#include <Wire.h>
#include <SparkFun_VL53L5CX_Library.h>
#include <SparkFun_BNO08x_Arduino_Library.h>

// Version - must match VERSION in the viewer (index.html / config.py)
#define FW_VERSION "0.2.0"

// Serial speed. Must match the baud selected in the viewer.
#define SERIAL_BAUD 921600

// Pin definitions
#define SDA_PIN 4
#define SCL_PIN 5
#define LPN_PIN 19

// I2C speed - 1MHz for fast data transfer
#define I2C_SPEED 1000000

SparkFun_VL53L5CX sensor;
VL53L5CX_ResultsData measurementData;
BNO08x imu;

bool imuAvailable = false;
bool sensorAvailable = false;
bool streaming = true;
bool sendReflectance = false;

uint8_t resolution = 8;   // 4 or 8 (zones per side)
uint8_t rangingHz = 15;
uint32_t frameSeq = 0;
uint32_t lastErrorReport = 0;

// Latest orientation, wxyz
float quatW = 1.0f, quatX = 0.0f, quatY = 0.0f, quatZ = 0.0f;

// Tare reference, wxyz. Output is conj(tare) * raw when tareActive.
bool tareActive = false;
float tareW = 1.0f, tareX = 0.0f, tareY = 0.0f, tareZ = 0.0f;

// Frame assembly buffer. Worst case 8x8 with reflectance is well under 1600.
char txBuf[1600];

// Command line buffer
char cmdBuf[32];
uint8_t cmdLen = 0;

// ---------------------------------------------------------------------------
// Fast number formatting (avoids sprintf in the hot path)
// ---------------------------------------------------------------------------

static inline char *appendInt(char *p, int32_t v) {
  if (v < 0) {
    *p++ = '-';
    v = -v;
  }
  char tmp[12];
  uint8_t n = 0;
  do {
    tmp[n++] = '0' + (v % 10);
    v /= 10;
  } while (v);
  while (n) *p++ = tmp[--n];
  return p;
}

// Writes a float in [-9, 9] with 6 decimal places. Used for quaternions.
static inline char *appendFixed6(char *p, float v) {
  if (v < 0) {
    *p++ = '-';
    v = -v;
  }
  uint32_t scaled = (uint32_t)(v * 1000000.0f + 0.5f);
  p = appendInt(p, (int32_t)(scaled / 1000000UL));
  *p++ = '.';
  uint32_t frac = scaled % 1000000UL;
  for (uint32_t d = 100000UL; d > 0; d /= 10) *p++ = '0' + (char)((frac / d) % 10);
  return p;
}

static inline char *appendStr(char *p, const char *s) {
  while (*s) *p++ = *s++;
  return p;
}

// ---------------------------------------------------------------------------
// Sensor configuration
// ---------------------------------------------------------------------------

uint8_t maxFrequencyFor(uint8_t res) {
  return (res == 4) ? 60 : 15;
}

// Applies resolution + frequency. Ranging must be stopped while reconfiguring.
bool applySensorConfig(uint8_t res, uint8_t hz) {
  if (!sensorAvailable) return false;

  uint8_t maxHz = maxFrequencyFor(res);
  if (hz < 1) hz = 1;
  if (hz > maxHz) hz = maxHz;

  sensor.stopRanging();
  delay(10);

  // On failure, restart ranging with whatever the sensor still holds so the
  // stream never dies silently.
  if (!sensor.setResolution(res * res)) { sensor.startRanging(); return false; }
  if (!sensor.setRangingFrequency(hz)) { sensor.startRanging(); return false; }

  resolution = res;
  rangingHz = hz;

  sensor.startRanging();
  return true;
}

// ---------------------------------------------------------------------------
// Replies
// ---------------------------------------------------------------------------

void sendIdentify() {
  char *p = txBuf;
  p = appendStr(p, "{\"id\":\"vl53l5cx\",\"v\":\"" FW_VERSION "\",\"res\":");
  p = appendInt(p, resolution);
  p = appendStr(p, ",\"hz\":");
  p = appendInt(p, rangingHz);
  p = appendStr(p, ",\"imu\":");
  p = appendInt(p, imuAvailable ? 1 : 0);
  p = appendStr(p, ",\"tof\":");
  p = appendInt(p, sensorAvailable ? 1 : 0);
  p = appendStr(p, ",\"streaming\":");
  p = appendInt(p, streaming ? 1 : 0);
  p = appendStr(p, ",\"baud\":");
  p = appendInt(p, SERIAL_BAUD);
  p = appendStr(p, "}\n");
  Serial.write((uint8_t *)txBuf, p - txBuf);
}

void sendStatus(const char *key, const char *value) {
  char *p = txBuf;
  p = appendStr(p, "{\"");
  p = appendStr(p, key);
  p = appendStr(p, "\":\"");
  p = appendStr(p, value);
  p = appendStr(p, "\"}\n");
  Serial.write((uint8_t *)txBuf, p - txBuf);
}

// ---------------------------------------------------------------------------
// Command handling
// ---------------------------------------------------------------------------

void handleCommand(const char *cmd, uint8_t len) {
  if (len == 0) return;

  switch (cmd[0]) {
    case '?':
      sendIdentify();
      break;

    case 's':
      streaming = (len > 1 && cmd[1] == '0') ? false : true;
      sendStatus("status", streaming ? "streaming" : "paused");
      break;

    case 'r': {
      uint8_t res = (len > 1 && cmd[1] == '4') ? 4 : 8;
      if (applySensorConfig(res, rangingHz)) {
        sendIdentify();
      } else {
        sendStatus("error", "reconfigure_failed");
      }
      break;
    }

    case 'f': {
      int hz = atoi(cmd + 1);
      if (hz <= 0) {
        sendStatus("error", "bad_frequency");
        break;
      }
      if (applySensorConfig(resolution, (uint8_t)hz)) {
        sendIdentify();
      } else {
        sendStatus("error", "reconfigure_failed");
      }
      break;
    }

    case 't':  // Tare: remember the current orientation as the new zero
      tareW = quatW;
      tareX = quatX;
      tareY = quatY;
      tareZ = quatZ;
      tareActive = true;
      sendStatus("status", "tared");
      break;

    case 'T':
      tareActive = false;
      sendStatus("status", "tare_cleared");
      break;

    case 'x':
      sendReflectance = (len > 1 && cmd[1] == '1');
      sendStatus("status", sendReflectance ? "reflectance_on" : "reflectance_off");
      break;

    case 'R':
      sendStatus("status", "rebooting");
      Serial.flush();
      delay(50);
      ESP.restart();
      break;

    default:
      sendStatus("error", "unknown_command");
      break;
  }
}

void pollCommands() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        handleCommand(cmdBuf, cmdLen);
        cmdLen = 0;
      }
    } else if (cmdLen < sizeof(cmdBuf) - 1) {
      cmdBuf[cmdLen++] = c;
    }
  }
}

// ---------------------------------------------------------------------------
// IMU
// ---------------------------------------------------------------------------

void readIMU() {
  if (!imuAvailable) return;

  if (imu.wasReset()) {
    // Re-enable game rotation vector if the IMU reset itself
    imu.enableGameRotationVector(10);
  }

  if (imu.getSensorEvent()) {
    if (imu.getSensorEventID() == SENSOR_REPORTID_GAME_ROTATION_VECTOR) {
      quatW = imu.getQuatReal();
      quatX = imu.getQuatI();
      quatY = imu.getQuatJ();
      quatZ = imu.getQuatK();
    }
  }
}

// Returns the orientation to transmit, applying the tare if one is set.
void getOutputQuat(float &w, float &x, float &y, float &z) {
  if (!tareActive) {
    w = quatW;
    x = quatX;
    y = quatY;
    z = quatZ;
    return;
  }
  // conj(tare) * raw
  float aw = tareW, ax = -tareX, ay = -tareY, az = -tareZ;
  float bw = quatW, bx = quatX, by = quatY, bz = quatZ;
  w = aw * bw - ax * bx - ay * by - az * bz;
  x = aw * bx + ax * bw + ay * bz - az * by;
  y = aw * by - ax * bz + ay * bw + az * bx;
  z = aw * bz + ax * by - ay * bx + az * bw;
}

// ---------------------------------------------------------------------------
// Frame output
// ---------------------------------------------------------------------------

void sendFrame() {
  const uint16_t zones = (uint16_t)resolution * resolution;
  char *p = txBuf;

  p = appendStr(p, "{\"v\":\"" FW_VERSION "\",\"seq\":");
  p = appendInt(p, (int32_t)frameSeq++);
  p = appendStr(p, ",\"t\":");
  p = appendInt(p, (int32_t)millis());
  p = appendStr(p, ",\"res\":");
  p = appendInt(p, resolution);
  p = appendStr(p, ",\"hz\":");
  p = appendInt(p, rangingHz);

  p = appendStr(p, ",\"distances\":[");
  for (uint16_t i = 0; i < zones; i++) {
    if (i) *p++ = ',';
    p = appendInt(p, measurementData.distance_mm[i]);
  }

  p = appendStr(p, "],\"status\":[");
  for (uint16_t i = 0; i < zones; i++) {
    if (i) *p++ = ',';
    p = appendInt(p, measurementData.target_status[i]);
  }
  *p++ = ']';

#ifndef VL53L5CX_DISABLE_REFLECTANCE
  if (sendReflectance) {
    p = appendStr(p, ",\"refl\":[");
    for (uint16_t i = 0; i < zones; i++) {
      if (i) *p++ = ',';
      p = appendInt(p, measurementData.reflectance[i]);
    }
    *p++ = ']';
  }
#endif

  if (imuAvailable) {
    float w, x, y, z;
    getOutputQuat(w, x, y, z);
    p = appendStr(p, ",\"quat\":[");
    p = appendFixed6(p, w);
    *p++ = ',';
    p = appendFixed6(p, x);
    *p++ = ',';
    p = appendFixed6(p, y);
    *p++ = ',';
    p = appendFixed6(p, z);
    *p++ = ']';
  }

  *p++ = '}';
  *p++ = '\n';

  Serial.write((uint8_t *)txBuf, p - txBuf);
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  sendStatus("status", "initializing");

  // Enable I2C on the ToF sensor by driving LPn HIGH
  pinMode(LPN_PIN, OUTPUT);
  digitalWrite(LPN_PIN, HIGH);
  delay(10);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_SPEED);
  sendStatus("status", "i2c_ready");

  // ToF sensor. On failure we keep running so the viewer can report why.
  if (sensor.begin()) {
    sensorAvailable = true;
    sendStatus("status", "sensor_found");
    sensor.setResolution(resolution * resolution);
    sensor.setRangingFrequency(rangingHz);
    sensor.startRanging();
    sendStatus("status", "ranging_started");
  } else {
    sendStatus("error", "sensor_init_failed");
  }

  // BNO08X IMU, shares the I2C bus. Try 0x4A then 0x4B.
  if (imu.begin(0x4A, Wire) || imu.begin(0x4B, Wire)) {
    imuAvailable = true;
    // Game rotation vector uses accel + gyro only, so magnetic interference
    // cannot pull the heading around.
    imu.enableGameRotationVector(10);
    sendStatus("status", "imu_ready");
  } else {
    sendStatus("status", "imu_not_found");
  }

  sendIdentify();
}

void loop() {
  pollCommands();
  readIMU();

  if (!sensorAvailable) {
    // Report periodically instead of blocking forever
    if (millis() - lastErrorReport > 2000) {
      lastErrorReport = millis();
      sendStatus("error", "sensor_unavailable");
    }
    delay(5);
    return;
  }

  if (streaming && sensor.isDataReady()) {
    if (sensor.getRangingData(&measurementData)) {
      sendFrame();
    }
  }

  delay(1);
}
