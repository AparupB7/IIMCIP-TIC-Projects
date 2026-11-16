#include <ESP32Servo.h>

#define TRIG_PIN  4
#define ECHO_PIN  5
#define SERVO_PIN 6

Servo radarServo;

long duration;
int distance;

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  radarServo.setPeriodHertz(50);
  radarServo.attach(SERVO_PIN, 500, 2400);

  delay(1000);
}

void loop() {
  // Left to Right
  for (int angle = 15; angle <= 165; angle += 2) {
    radarServo.write(angle);
    delay(28);

    distance = calculateDistance();

    Serial.print(angle);
    Serial.print(",");
    Serial.println(distance);
  }

  // Right to Left
  for (int angle = 165; angle >= 15; angle -= 2) {
    radarServo.write(angle);
    delay(28);

    distance = calculateDistance();

    Serial.print(angle);
    Serial.print(",");
    Serial.println(distance);
  }
}

int calculateDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH, 30000);

  distance = duration * 0.034 / 2;

  if (distance <= 0 || distance > 400) {
    distance = 400;
  }

  return distance;
}
