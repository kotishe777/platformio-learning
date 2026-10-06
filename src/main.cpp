/*
 * Button debounce lab. Serial Monitor at 115200, type a key (no Enter):
 *   1 - Task 1, no debounce: every FALLING interrupt is counted as an event
 *   2 - Task 2, time-based: interrupts within 50 ms of the last event are ignored
 *   3 - Task 3, state-based: an event is accepted only if the button is still pressed
 *   4 - Task 4, polling: no interrupt, button read every 5 ms by a state machine
 *   r - reset counters | h - help
 * Press RST on the board to return to defaults (method 1, counters at zero).
 * Task 5 uses the same code: add the RC filter (100 nF + 100 Ohm) to the circuit
 * and repeat methods 1-4.
 */

#include <Arduino.h>

struct Config {
  static constexpr uint8_t  buttonPin    = 6;
  static constexpr uint8_t  reactionPin  = 15;
  static constexpr uint32_t baudRate     = 115200;
  static constexpr uint32_t burstGapUs   = 100000;
  static constexpr uint32_t timeDebounceUs = 50000;
  static constexpr uint32_t settleUs     = 20000;
  static constexpr uint32_t pollPeriodMs = 5;
  static constexpr uint8_t  stableSamples = 4;
};

enum class Method : uint8_t { None, TimeBased, StateBased, Polling };

namespace {

volatile uint32_t irqCount     = 0;
volatile uint32_t lastIrqUs    = 0;
volatile uint32_t burstStartUs = 0;

void IRAM_ATTR onButtonIsr() {
  const uint32_t now = micros();
  if (now - lastIrqUs > Config::burstGapUs) {
    burstStartUs = now;
  }
  lastIrqUs = now;
  irqCount++;
}

class DebounceLab {
public:
  void init() {
    Serial.begin(Config::baudRate);

    pinMode(Config::buttonPin, INPUT);
    pinMode(Config::reactionPin, OUTPUT);
    digitalWrite(Config::reactionPin, LOW);

    select(Method::None);
  }

  void update() {
    handleCommands();

    switch (method_) {
      case Method::None:       updateNone(); break;
      case Method::TimeBased:  updateTimeBased(); break;
      case Method::StateBased: updateStateBased(); break;
      case Method::Polling:    updatePolling(); break;
    }
  }

private:
  enum class PollState : uint8_t { Released, MaybePressed, Pressed, MaybeReleased };

  static const char* methodName(Method m) {
    switch (m) {
      case Method::TimeBased:  return "TIME ";
      case Method::StateBased: return "STATE";
      case Method::Polling:    return "POLL ";
      default:                 return "NONE ";
    }
  }

  void handleCommands() {
    while (Serial.available() > 0) {
      const char key = static_cast<char>(Serial.read());
      switch (key) {
        case '1': select(Method::None); break;
        case '2': select(Method::TimeBased); break;
        case '3': select(Method::StateBased); break;
        case '4': select(Method::Polling); break;
        case 'r': resetCounters(); break;
        case 'h': printHelp(); break;
        default: break;
      }
    }
  }

  void select(Method method) {
    method_ = method;

    if (method_ == Method::Polling) {
      detachInterrupt(digitalPinToInterrupt(Config::buttonPin));
      irqAttached_ = false;
    } else if (!irqAttached_) {
      attachInterrupt(digitalPinToInterrupt(Config::buttonPin), onButtonIsr, FALLING);
      irqAttached_ = true;
    }

    resetCounters();
    Serial.printf("\n=== Method: %s ===\n", methodName(method_));
    printHelp();
  }

  void resetCounters() {
    noInterrupts();
    irqCount = 0;
    interrupts();

    seenIrq_ = 0;
    accepted_ = 0;
    pending_ = false;
    lastAcceptedUs_ = micros() - Config::timeDebounceUs;
    pollState_ = PollState::Released;
    samples_ = 0;
    lastPollMs_ = millis();
    Serial.println("Counters reset");
  }

  static void printHelp() {
    Serial.println("Keys: 1 none | 2 time-based | 3 state-based | 4 polling | r reset | h help");
  }

  bool takeNewIrqs(uint32_t& newCount, uint32_t& lastUs) {
    noInterrupts();
    const uint32_t count = irqCount;
    lastUs = lastIrqUs;
    interrupts();

    newCount = count - seenIrq_;
    seenIrq_ = count;
    return newCount > 0;
  }

  uint32_t burstStart() const {
    noInterrupts();
    const uint32_t start = burstStartUs;
    interrupts();
    return start;
  }

  void react(uint32_t startUs) {
    accepted_++;
    reactionLevel_ = !reactionLevel_;
    digitalWrite(Config::reactionPin, reactionLevel_ ? HIGH : LOW);

    const uint32_t nowUs = micros();
    const bool pinHigh = digitalRead(Config::buttonPin) == HIGH;

    Serial.printf("[%s] event #%3lu | raw IRQ %3lu | latency %7.2f ms | pin %s%s\n",
                  methodName(method_),
                  static_cast<unsigned long>(accepted_),
                  static_cast<unsigned long>(seenIrq_),
                  (nowUs - startUs) / 1000.0f,
                  pinHigh ? "HIGH" : "LOW ",
                  pinHigh ? "  <- release" : "");
  }

  void updateNone() {
    uint32_t newCount = 0;
    uint32_t lastUs = 0;
    if (!takeNewIrqs(newCount, lastUs)) {
      return;
    }
    for (uint32_t i = 0; i < newCount; i++) {
      react(burstStart());
    }
  }

  void updateTimeBased() {
    uint32_t newCount = 0;
    uint32_t lastUs = 0;
    if (!takeNewIrqs(newCount, lastUs)) {
      return;
    }
    if (lastUs - lastAcceptedUs_ < Config::timeDebounceUs) {
      return;
    }
    lastAcceptedUs_ = lastUs;
    react(burstStart());
  }

  void updateStateBased() {
    uint32_t newCount = 0;
    uint32_t lastUs = 0;
    if (takeNewIrqs(newCount, lastUs)) {
      pending_ = true;
      return;
    }
    if (!pending_) {
      return;
    }

    noInterrupts();
    const uint32_t quietSince = lastIrqUs;
    interrupts();
    if (micros() - quietSince < Config::settleUs) {
      return;
    }
    pending_ = false;

    if (digitalRead(Config::buttonPin) == LOW) {
      react(burstStart());
    }
  }

  void updatePolling() {
    const uint32_t nowMs = millis();
    if (nowMs - lastPollMs_ < Config::pollPeriodMs) {
      return;
    }
    lastPollMs_ = nowMs;

    const bool low = digitalRead(Config::buttonPin) == LOW;

    switch (pollState_) {
      case PollState::Released:
        if (low) {
          pollState_ = PollState::MaybePressed;
          samples_ = 1;
          pollStartUs_ = micros();
        }
        break;

      case PollState::MaybePressed:
        if (!low) {
          pollState_ = PollState::Released;
        } else if (++samples_ >= Config::stableSamples) {
          pollState_ = PollState::Pressed;
          react(pollStartUs_);
        }
        break;

      case PollState::Pressed:
        if (!low) {
          pollState_ = PollState::MaybeReleased;
          samples_ = 1;
        }
        break;

      case PollState::MaybeReleased:
        if (low) {
          pollState_ = PollState::Pressed;
        } else if (++samples_ >= Config::stableSamples) {
          pollState_ = PollState::Released;
        }
        break;
    }
  }

  Method method_ = Method::None;
  bool irqAttached_ = false;

  uint32_t seenIrq_ = 0;
  uint32_t accepted_ = 0;
  bool reactionLevel_ = false;

  uint32_t lastAcceptedUs_ = 0;
  bool pending_ = false;

  PollState pollState_ = PollState::Released;
  uint8_t samples_ = 0;
  uint32_t lastPollMs_ = 0;
  uint32_t pollStartUs_ = 0;
};

DebounceLab app;

}  // namespace

void setup() {
  app.init();
}

void loop() {
  app.update();
}
