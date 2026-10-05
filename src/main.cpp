#include <Arduino.h>

struct Config {
  static constexpr uint32_t baudRate = 115200;
  static constexpr bool     logToggles = true;
};

class BlinkingLed {
public:
  constexpr BlinkingLed(const char* name, uint8_t pin, uint32_t intervalMs)
    : name_(name), pin_(pin), intervalMs_(intervalMs) {}

  void init(uint32_t nowMs) {
    pinMode(pin_, OUTPUT);
    digitalWrite(pin_, LOW);
    lastToggleMs_ = nowMs;
  }

  void update(uint32_t nowMs) {
    if (nowMs - lastToggleMs_ < intervalMs_) {
      return;
    }

    lastToggleMs_ += intervalMs_;
    if (nowMs - lastToggleMs_ >= intervalMs_) {
      lastToggleMs_ = nowMs;
    }

    on_ = !on_;
    digitalWrite(pin_, on_ ? HIGH : LOW);

    if (Config::logToggles) {
      Serial.printf("%7lu ms | %s %s\n", static_cast<unsigned long>(nowMs), name_, on_ ? "ON " : "OFF");
    }
  }

private:
  const char* const name_;
  const uint8_t pin_;
  const uint32_t intervalMs_;
  uint32_t lastToggleMs_ = 0;
  bool on_ = false;
};

namespace {

BlinkingLed leds[] = {
  {"LED1", 15, 200},
  {"LED2", 16, 500},
  {"LED3", 17, 1000},
};

}  // namespace

void setup() {
  Serial.begin(Config::baudRate);

  const uint32_t nowMs = millis();
  for (BlinkingLed& led : leds) {
    led.init(nowMs);
  }
}

void loop() {
  const uint32_t nowMs = millis();
  for (BlinkingLed& led : leds) {
    led.update(nowMs);
  }
}
