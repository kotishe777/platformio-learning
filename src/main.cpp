#include <Arduino.h>

struct Config {
  static constexpr uint8_t  ledPin           = 15;
  static constexpr uint8_t  buttonPin        = 6;
  static constexpr uint32_t baudRate         = 115200;
  static constexpr uint32_t blinkIntervalMs  = 500;
  static constexpr uint32_t debounceMs       = 50;
  static constexpr uint32_t reportEveryLoops = 1000;
};

enum class LedState : uint8_t { Off, On };
enum class LedMode : uint8_t { Blink, AlwaysOn, AlwaysOff };

class Led {
public:
  explicit constexpr Led(uint8_t pin) : pin_(pin) {}

  void init() {
    pinMode(pin_, OUTPUT);
    set(LedState::Off);
  }

  void set(LedState state) {
    state_ = state;
    digitalWrite(pin_, state_ == LedState::On ? HIGH : LOW);
  }

  void toggle() {
    set(state_ == LedState::On ? LedState::Off : LedState::On);
  }

private:
  const uint8_t pin_;
  LedState state_ = LedState::Off;
};

namespace {

volatile bool buttonEvent = false;

void IRAM_ATTR onButtonIsr() {
  buttonEvent = true;
}

class BlinkApp {
public:
  void init() {
    Serial.begin(Config::baudRate);
    led_.init();

    pinMode(Config::buttonPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(Config::buttonPin), onButtonIsr, FALLING);

    applyMode(millis());
  }

  void update() {
    const uint32_t startUs = micros();
    const uint32_t nowMs = millis();

    handleButton(nowMs);
    updateLed(nowMs);

    measureLoop(micros() - startUs);
  }

private:
  static constexpr LedMode nextMode(LedMode mode) {
    return mode == LedMode::Blink    ? LedMode::AlwaysOn
         : mode == LedMode::AlwaysOn ? LedMode::AlwaysOff
                                     : LedMode::Blink;
  }

  static const char* modeName(LedMode mode) {
    switch (mode) {
      case LedMode::AlwaysOn:  return "ALWAYS ON";
      case LedMode::AlwaysOff: return "ALWAYS OFF";
      default:                 return "BLINK";
    }
  }

  void handleButton(uint32_t nowMs) {
    noInterrupts();
    const bool event = buttonEvent;
    buttonEvent = false;
    interrupts();

    if (event) {
      pending_ = true;
      lastEdgeMs_ = nowMs;
    }

    if (!pending_ || nowMs - lastEdgeMs_ < Config::debounceMs) {
      return;
    }
    pending_ = false;

    if (digitalRead(Config::buttonPin) == LOW) {
      mode_ = nextMode(mode_);
      applyMode(nowMs);
      Serial.printf("Mode: %s\n", modeName(mode_));
    }
  }

  void applyMode(uint32_t nowMs) {
    switch (mode_) {
      case LedMode::AlwaysOn:
        led_.set(LedState::On);
        break;
      case LedMode::AlwaysOff:
        led_.set(LedState::Off);
        break;
      case LedMode::Blink:
        led_.set(LedState::On);
        lastToggleMs_ = nowMs;
        break;
    }
  }

  void updateLed(uint32_t nowMs) {
    if (mode_ != LedMode::Blink) {
      return;
    }
    if (nowMs - lastToggleMs_ >= Config::blinkIntervalMs) {
      lastToggleMs_ = nowMs;
      led_.toggle();
    }
  }

  void measureLoop(uint32_t elapsedUs) {
    loopSumUs_ += elapsedUs;
    if (elapsedUs < loopMinUs_) {
      loopMinUs_ = elapsedUs;
    }
    if (elapsedUs > loopMaxUs_) {
      loopMaxUs_ = elapsedUs;
    }

    if (++loopCount_ < Config::reportEveryLoops) {
      return;
    }

    Serial.printf("Loop: avg %.2f us | min %u us | max %u us\n",
                  static_cast<float>(loopSumUs_) / loopCount_, loopMinUs_, loopMaxUs_);

    loopCount_ = 0;
    loopSumUs_ = 0;
    loopMinUs_ = UINT32_MAX;
    loopMaxUs_ = 0;
  }

  Led led_{Config::ledPin};
  LedMode mode_ = LedMode::Blink;

  uint32_t lastToggleMs_ = 0;
  uint32_t lastEdgeMs_ = 0;
  bool pending_ = false;

  uint32_t loopCount_ = 0;
  uint32_t loopSumUs_ = 0;
  uint32_t loopMinUs_ = UINT32_MAX;
  uint32_t loopMaxUs_ = 0;
};

BlinkApp app;

}  // namespace

void setup() {
  app.init();
}

void loop() {
  app.update();
}
