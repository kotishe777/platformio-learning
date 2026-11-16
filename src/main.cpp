/*
 * Module 3.5 hometask — potentiometer controls servo angle 1:1 (ESP32Servo library)
 * Board: ESP32-S3 DevKitC-1 N16R8, framework Arduino (Core 3.x)
 * Pot: 3V3 -> 10k pot -> GND, wiper -> GPIO7 (ADC1_CH6), about 270 deg of rotation
 * Servo SG90: signal GPIO18, power +5V from separate supply, common GND, 470 uF cap
 * Pot angle maps 1:1 to servo angle; pot angles above 180 deg are clipped to 180
 * Angle from the leftmost position is logged every 250 ms
 * Non-blocking: millis() timing, no delay()
 */
#include <Arduino.h>
#include <ESP32Servo.h>

struct Config {
  static constexpr uint8_t  potPin         = 7;
  static constexpr uint8_t  servoPin       = 18;
  static constexpr int      servoMinUs     = 500;
  static constexpr int      servoMaxUs     = 2400;
  static constexpr int      servoRangeDeg  = 180;
  static constexpr int      potRangeDeg    = 270;
  static constexpr int      adcMax         = 4095;
  static constexpr uint8_t  samplesPerRead = 16;
  static constexpr uint32_t updateMs       = 20;
  static constexpr uint32_t logMs          = 250;
};

static_assert(Config::potRangeDeg >= Config::servoRangeDeg, "Pot range must cover servo range");
static_assert(Config::servoMinUs < Config::servoMaxUs, "Min pulse must be below max pulse");

Servo    servo;
uint32_t lastUpdateMs = 0;
uint32_t lastLogMs    = 0;
int      potRaw       = 0;
int      potAngle     = 0;
int      servoAngle   = 0;

int readPotRaw() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < Config::samplesPerRead; ++i) {
    sum += analogRead(Config::potPin);
  }
  return static_cast<int>(sum / Config::samplesPerRead);
}

int potAngleFromRaw(int raw) {
  return raw * Config::potRangeDeg / Config::adcMax;
}

int clipToServo(int angle) {
  return angle > Config::servoRangeDeg ? Config::servoRangeDeg : angle;
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);

  servo.setPeriodHertz(50);
  servo.attach(Config::servoPin, Config::servoMinUs, Config::servoMaxUs);

  lastUpdateMs = millis();
  lastLogMs    = millis();
  Serial.println("Servo 1:1 ready: pot 0..180 deg -> servo 0..180 deg, above 180 is clipped");
}

void loop() {
  const uint32_t now = millis();

  if (now - lastUpdateMs >= Config::updateMs) {
    lastUpdateMs += Config::updateMs;
    potRaw     = readPotRaw();
    potAngle   = potAngleFromRaw(potRaw);
    servoAngle = clipToServo(potAngle);
    servo.write(servoAngle);
  }

  if (now - lastLogMs >= Config::logMs) {
    lastLogMs += Config::logMs;
    Serial.printf("pot raw=%4d  pot=%3d deg  servo=%3d deg from left%s\n",
                  potRaw, potAngle, servoAngle,
                  potAngle > Config::servoRangeDeg ? "  (clipped)" : "");
  }
}