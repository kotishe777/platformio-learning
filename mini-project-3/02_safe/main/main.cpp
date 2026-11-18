/*
 * Module 3 mini-project — part 2: encoder safe (PIN code lock)
 * Board: ESP32-S3 DevKitC-1 N16R8, framework ESP-IDF 5.5
 * Encoder: CLK GPIO4, DT GPIO5; button GPIO6 -> GND; servo lock GPIO14; buzzer GPIO17
 * Each tick in one direction increments the digit 0..9 (cyclic); direction change
 * confirms it and starts the next digit at 0; a reverse tick after the last digit checks the code
 * Button resets the input and counts as an attempt; 3 attempts, then alarm and lock until reboot
 * Correct code: success melody and servo opens; button closes it again. Input logged with \b
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "button.hpp"
#include "buzzer.hpp"
#include "encoder.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "servo.hpp"

struct Config {
  static constexpr ServoConfig    servo{GPIO_NUM_14, LEDC_TIMER_0, LEDC_CHANNEL_0,
                                        10, 170, 500, 2500};
  static constexpr gpio_num_t     encoderA      = GPIO_NUM_4;
  static constexpr gpio_num_t     encoderB      = GPIO_NUM_5;
  static constexpr gpio_num_t     buttonPin     = GPIO_NUM_6;
  static constexpr int            direction     = 1;
  static constexpr gpio_num_t     buzzerPin     = GPIO_NUM_17;
  static constexpr ledc_timer_t   buzzerTimer   = LEDC_TIMER_1;
  static constexpr ledc_channel_t buzzerChannel = LEDC_CHANNEL_1;
  static constexpr size_t         codeLength    = 4;
  static constexpr uint8_t        secret[codeLength] = {4, 2, 0, 7};
  static constexpr int            maxAttempts   = 3;
  static constexpr int            lockedDeg     = 20;
  static constexpr int            openDeg       = 110;
  static constexpr uint16_t       confirmBeepHz = 1500;
  static constexpr uint16_t       confirmBeepMs = 40;
  static constexpr uint16_t       resetBeepHz   = 600;
  static constexpr uint16_t       resetBeepMs   = 150;
  static constexpr uint32_t       periodMs      = 10;
};

constexpr bool secretIsValid() {
  for (size_t i = 0; i < Config::codeLength; ++i) {
    if (Config::secret[i] > 9) {
      return false;
    }
  }
  return true;
}

static_assert(Config::codeLength >= 1 && Config::codeLength <= 8, "Code must have 1..8 digits");
static_assert(secretIsValid(), "Every secret digit must be 0..9");
static_assert(Config::maxAttempts > 0, "At least one attempt");

namespace {
constexpr Tone successMelody[] = {{523, 120}, {659, 120}, {784, 120}, {1047, 350}};
constexpr Tone wrongMelody[]   = {{330, 150}, {0, 80}, {262, 300}};
constexpr Tone alarmMelody[]   = {{880, 200}, {660, 200}, {880, 200}, {660, 200},
                                  {880, 200}, {660, 200}, {880, 200}, {660, 200},
                                  {880, 200}, {660, 200}, {880, 200}, {660, 400}};

template <size_t N>
void playMelody(Buzzer& buzzer, const Tone (&melody)[N], uint32_t now) {
  buzzer.play(melody, N, now);
}

uint32_t nowMs() {
  return pdTICKS_TO_MS(xTaskGetTickCount());
}

enum class EntryResult : uint8_t { None, DigitConfirmed, Complete };

class CodeEntry {
 public:
  void reset() {
    count_   = 0;
    active_  = false;
    dir_     = 0;
    current_ = 0;
  }

  EntryResult tick(int sign) {
    if (!active_) {
      startDigit(sign);
      return EntryResult::None;
    }
    if (sign == dir_) {
      current_ = (current_ + 1) % 10;
      printf("\b%d", current_);
      fflush(stdout);
      return EntryResult::None;
    }
    digits_[count_++] = current_;
    if (count_ == Config::codeLength) {
      active_ = false;
      printf("\n");
      fflush(stdout);
      return EntryResult::Complete;
    }
    startDigit(sign);
    return EntryResult::DigitConfirmed;
  }

  bool matchesSecret() const {
    if (count_ != Config::codeLength) {
      return false;
    }
    for (size_t i = 0; i < Config::codeLength; ++i) {
      if (digits_[i] != Config::secret[i]) {
        return false;
      }
    }
    return true;
  }

 private:
  void startDigit(int sign) {
    active_  = true;
    dir_     = sign;
    current_ = 0;
    printf(count_ == 0 ? "0" : " 0");
    fflush(stdout);
  }

  uint8_t digits_[Config::codeLength]{};
  size_t  count_   = 0;
  bool    active_  = false;
  int     dir_     = 0;
  int     current_ = 0;
};

enum class SafeState : uint8_t { Entering, Open, Locked };

void startAttempt(CodeEntry& entry, int attemptsLeft) {
  entry.reset();
  printf("\nAttempt %d/%d, code: ", Config::maxAttempts - attemptsLeft + 1, Config::maxAttempts);
  fflush(stdout);
}
}  // namespace

extern "C" void app_main() {
  Servo servo(Config::servo);
  servo.init(Config::lockedDeg);

  Encoder encoder(Config::encoderA, Config::encoderB);
  encoder.init();

  Button button(Config::buttonPin);
  button.init();

  Buzzer buzzer(Config::buzzerPin, Config::buzzerTimer, Config::buzzerChannel);
  buzzer.init();

  printf("\nSAFE: enter %u digits. Turn = digit up, change direction = confirm.\n",
         static_cast<unsigned>(Config::codeLength));
  printf("Button = reset (counts as an attempt). Attempts: %d\n", Config::maxAttempts);

  CodeEntry entry;
  SafeState state        = SafeState::Entering;
  int       attemptsLeft = Config::maxAttempts;
  startAttempt(entry, attemptsLeft);

  auto useAttempt = [&](uint32_t now, const char* reason) {
    --attemptsLeft;
    if (attemptsLeft == 0) {
      state = SafeState::Locked;
      playMelody(buzzer, alarmMelody, now);
      printf("\n%s. ALARM: no attempts left, safe blocked until reboot (RST).\n", reason);
      fflush(stdout);
      return;
    }
    printf("\n%s. Attempts left: %d", reason, attemptsLeft);
    startAttempt(entry, attemptsLeft);
  };

  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    const uint32_t    now     = nowMs();
    const int         clicks  = encoder.readClicks() * Config::direction;
    const int         sign    = clicks > 0 ? 1 : -1;
    const ButtonEvent press   = button.update(now);
    const bool        pressed = press != ButtonEvent::None;

    switch (state) {
      case SafeState::Entering:
        for (int i = 0; i < clicks * sign && state == SafeState::Entering; ++i) {
          const EntryResult result = entry.tick(sign);
          if (result == EntryResult::DigitConfirmed) {
            buzzer.beep(Config::confirmBeepHz, Config::confirmBeepMs, now);
          } else if (result == EntryResult::Complete) {
            if (entry.matchesSecret()) {
              state = SafeState::Open;
              servo.setAngle(Config::openDeg);
              playMelody(buzzer, successMelody, now);
              printf("ACCESS GRANTED: lock open. Press the button to lock again.\n");
              fflush(stdout);
            } else {
              playMelody(buzzer, wrongMelody, now);
              useAttempt(now, "WRONG CODE");
            }
          }
        }
        if (pressed && state == SafeState::Entering) {
          buzzer.beep(Config::resetBeepHz, Config::resetBeepMs, now);
          useAttempt(now, "RESET");
        }
        break;
      case SafeState::Open:
        if (pressed) {
          state = SafeState::Entering;
          servo.setAngle(Config::lockedDeg);
          attemptsLeft = Config::maxAttempts;
          printf("\nLocked again.");
          startAttempt(entry, attemptsLeft);
        }
        break;
      case SafeState::Locked:
        break;
    }

    buzzer.update(now);

    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::periodMs));
  }
}
