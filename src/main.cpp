#include <Arduino.h>

struct Config {
  static constexpr uint8_t  relayPin     = 5;
  static constexpr uint8_t  contactPin   = 7;
  static constexpr uint32_t baudRate     = 115200;
  static constexpr uint8_t  measurements = 10;
  static constexpr uint32_t settleUs     = 20000;
  static constexpr uint32_t timeoutUs    = 200000;
  static constexpr uint32_t holdMs       = 500;
  static constexpr uint32_t promptMs     = 2000;
};

struct Result {
  uint32_t delayUs = 0;
  uint32_t bounceUs = 0;
  uint16_t edges = 0;
  bool ok = false;
};

enum class Phase : uint8_t { WaitStart, Switching, Holding };

namespace {

volatile bool     armed       = false;
volatile uint16_t edgeCount   = 0;
volatile uint32_t firstEdgeUs = 0;
volatile uint32_t lastEdgeUs  = 0;

void IRAM_ATTR onContactIsr() {
  if (!armed) {
    return;
  }
  const uint32_t now = micros();
  if (edgeCount == 0) {
    firstEdgeUs = now;
  }
  lastEdgeUs = now;
  edgeCount++;
}

class RelayTimer {
public:
  void init() {
    Serial.begin(Config::baudRate);

    pinMode(Config::relayPin, OUTPUT);
    digitalWrite(Config::relayPin, LOW);

    pinMode(Config::contactPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(Config::contactPin), onContactIsr, CHANGE);
  }

  void update() {
    switch (phase_) {
      case Phase::WaitStart: waitStart(); break;
      case Phase::Switching: checkSwitching(); break;
      case Phase::Holding:   checkHolding(); break;
    }
  }

private:
  void waitStart() {
    if (Serial.available() > 0) {
      while (Serial.available() > 0) {
        Serial.read();
      }
      index_ = 0;
      Serial.printf("\nStarting %u measurements...\n", Config::measurements);
      startSwitch(true);
      return;
    }

    if (millis() - lastPromptMs_ >= Config::promptMs) {
      lastPromptMs_ = millis();
      Serial.println("Press Enter to start the measurements");
    }
  }

  void startSwitch(bool turnOn) {
    switchingOn_ = turnOn;

    noInterrupts();
    edgeCount = 0;
    armed = true;
    interrupts();

    commandUs_ = micros();
    digitalWrite(Config::relayPin, turnOn ? HIGH : LOW);
    phase_ = Phase::Switching;
  }

  void checkSwitching() {
    noInterrupts();
    const uint16_t edges = edgeCount;
    const uint32_t first = firstEdgeUs;
    const uint32_t last  = lastEdgeUs;
    interrupts();

    const uint32_t now = micros();
    Result result;

    if (edges == 0) {
      if (now - commandUs_ < Config::timeoutUs) {
        return;
      }
    } else {
      if (now - last < Config::settleUs) {
        return;
      }
      const bool expectedLevel = switchingOn_ ? LOW : HIGH;
      result.delayUs  = first - commandUs_;
      result.bounceUs = last - first;
      result.edges    = edges;
      result.ok       = digitalRead(Config::contactPin) == expectedLevel;
    }

    armed = false;
    store(result);
    printResult(result);

    holdStartMs_ = millis();
    phase_ = Phase::Holding;
  }

  void checkHolding() {
    if (millis() - holdStartMs_ < Config::holdMs) {
      return;
    }

    if (switchingOn_) {
      startSwitch(false);
      return;
    }

    if (++index_ < Config::measurements) {
      startSwitch(true);
      return;
    }

    printSummary();
    phase_ = Phase::WaitStart;
  }

  void store(const Result& result) {
    if (switchingOn_) {
      on_[index_] = result;
    } else {
      off_[index_] = result;
    }
  }

  void printResult(const Result& r) const {
    const char* direction = switchingOn_ ? "ON " : "OFF";
    if (r.edges == 0) {
      Serial.printf("#%2u %s | TIMEOUT: contact did not switch\n", index_ + 1, direction);
      return;
    }
    Serial.printf("#%2u %s | delay %6u us | bounce %5u us | edges %2u%s\n",
                  index_ + 1, direction, r.delayUs, r.bounceUs, r.edges,
                  r.ok ? "" : " | WRONG FINAL STATE");
  }

  static void printStats(const char* name, const Result (&results)[Config::measurements]) {
    uint32_t sum = 0;
    uint32_t bounceSum = 0;
    uint32_t minUs = UINT32_MAX;
    uint32_t maxUs = 0;
    uint8_t valid = 0;

    for (const Result& r : results) {
      if (!r.ok) {
        continue;
      }
      sum += r.delayUs;
      bounceSum += r.bounceUs;
      if (r.delayUs < minUs) minUs = r.delayUs;
      if (r.delayUs > maxUs) maxUs = r.delayUs;
      valid++;
    }

    if (valid == 0) {
      Serial.printf("%s: no valid measurements\n", name);
      return;
    }

    Serial.printf("%s: avg %.2f ms | min %.2f ms | max %.2f ms | avg bounce %.3f ms | valid %u/%u\n",
                  name,
                  sum / 1000.0f / valid, minUs / 1000.0f, maxUs / 1000.0f,
                  bounceSum / 1000.0f / valid,
                  valid, Config::measurements);
  }

  void printSummary() const {
    Serial.println("\n===== Summary =====");
    printStats("Turn ON ", on_);
    printStats("Turn OFF", off_);
    Serial.println("===================\n");
  }

  Phase phase_ = Phase::WaitStart;
  bool switchingOn_ = false;
  uint8_t index_ = 0;

  uint32_t commandUs_ = 0;
  uint32_t holdStartMs_ = 0;
  uint32_t lastPromptMs_ = 0;

  Result on_[Config::measurements];
  Result off_[Config::measurements];
};

RelayTimer app;

}  // namespace

void setup() {
  app.init();
}

void loop() {
  app.update();
}
