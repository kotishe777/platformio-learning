#include <Arduino.h>

struct Config {
  static constexpr uint8_t  potPin      = 4;
  static constexpr uint8_t  pwmPin      = 5;
  static constexpr uint8_t  uartTxPin   = 17;
  static constexpr uint8_t  uartRxPin   = 18;
  static constexpr uint32_t baudRate    = 115200;
  static constexpr uint32_t uartBaud    = 9600;
  static constexpr uint32_t pwmPeriodUs = 1000;
  static constexpr uint16_t adcMax      = 4095;
  static constexpr uint8_t  adcBits     = 12;
  static constexpr uint32_t adcReadMs   = 20;
  static constexpr uint8_t  filterShift = 3;
  static constexpr uint32_t reportMs    = 500;
};

class SoftPwm {
public:
  constexpr SoftPwm(uint8_t pin, uint32_t periodUs) : pin_(pin), periodUs_(periodUs) {}

  void init() {
    pinMode(pin_, OUTPUT);
    digitalWrite(pin_, LOW);
    periodStartUs_ = micros();
  }

  void setHighUs(uint32_t highUs) {
    highUs_ = highUs > periodUs_ ? periodUs_ : highUs;
  }

  void update(uint32_t nowUs) {
    if (nowUs - periodStartUs_ >= periodUs_) {
      periodStartUs_ += periodUs_;
      if (nowUs - periodStartUs_ >= periodUs_) {
        periodStartUs_ = nowUs;
      }
    }

    const bool high = nowUs - periodStartUs_ < highUs_;
    if (high != level_) {
      level_ = high;
      digitalWrite(pin_, high ? HIGH : LOW);
    }
  }

  uint32_t highUs() const { return highUs_; }
  uint32_t lowUs() const { return periodUs_ - highUs_; }
  float dutyPercent() const { return 100.0f * highUs_ / periodUs_; }

private:
  const uint8_t pin_;
  const uint32_t periodUs_;
  uint32_t highUs_ = 0;
  uint32_t periodStartUs_ = 0;
  bool level_ = false;
};

struct UartPattern {
  char key;
  uint8_t value;
  const char* name;
};

constexpr UartPattern uartPatterns[] = {
  {'1', 0x00, "0x00 (expected duty 10%)"},
  {'2', 0x0F, "0x0F (expected duty 50%, 2 pulses per frame)"},
  {'3', 0x55, "0x55 (expected duty 50%, 5 pulses per frame)"},
  {'4', 0xFF, "0xFF (expected duty 90%)"},
};

namespace {

class MotorApp {
public:
  void init() {
    Serial.begin(Config::baudRate);
    analogReadResolution(Config::adcBits);

    pwm_.init();
    adcAccum_ = static_cast<int32_t>(analogRead(Config::potPin)) << Config::filterShift;

    Serial1.begin(Config::uartBaud, SERIAL_8N1, Config::uartRxPin, Config::uartTxPin);
  }

  void update() {
    const uint32_t nowUs = micros();
    const uint32_t nowMs = millis();

    pwm_.update(nowUs);
    readPotentiometer(nowMs);
    handleCommands();
    feedUart();
    report(nowMs);
  }

private:
  void readPotentiometer(uint32_t nowMs) {
    if (nowMs - lastAdcMs_ < Config::adcReadMs) {
      return;
    }
    lastAdcMs_ = nowMs;

    const int32_t raw = analogRead(Config::potPin);
    adcAccum_ += raw - (adcAccum_ >> Config::filterShift);

    pwm_.setHighUs(static_cast<uint32_t>(filteredAdc()) * Config::pwmPeriodUs / Config::adcMax);
  }

  void handleCommands() {
    while (Serial.available() > 0) {
      const char key = static_cast<char>(Serial.read());

      if (key == '0') {
        uartActive_ = false;
        Serial.println("UART pattern: off (TX idle HIGH)");
        continue;
      }

      for (const UartPattern& pattern : uartPatterns) {
        if (pattern.key == key) {
          uartActive_ = true;
          uartValue_ = pattern.value;
          Serial.printf("UART pattern: %s\n", pattern.name);
        }
      }
    }
  }

  void feedUart() {
    if (!uartActive_) {
      return;
    }
    while (Serial1.availableForWrite() > 0) {
      Serial1.write(uartValue_);
    }
  }

  void report(uint32_t nowMs) {
    if (nowMs - lastReportMs_ < Config::reportMs) {
      return;
    }
    lastReportMs_ = nowMs;

    Serial.printf("ADC %4ld | duty %5.1f %% | high %4u us | low %4u us | UART %s | keys: 0 off, 1-4 patterns\n",
                  static_cast<long>(filteredAdc()), pwm_.dutyPercent(), pwm_.highUs(), pwm_.lowUs(),
                  uartActive_ ? "on" : "off");
  }

  int32_t filteredAdc() const { return adcAccum_ >> Config::filterShift; }

  SoftPwm pwm_{Config::pwmPin, Config::pwmPeriodUs};
  int32_t adcAccum_ = 0;

  bool uartActive_ = false;
  uint8_t uartValue_ = 0x00;

  uint32_t lastAdcMs_ = 0;
  uint32_t lastReportMs_ = 0;
};

MotorApp app;

}  // namespace

void setup() {
  app.init();
}

void loop() {
  app.update();
}
