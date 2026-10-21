#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct Config {
  static constexpr gpio_num_t redPin    = GPIO_NUM_15;
  static constexpr gpio_num_t yellowPin = GPIO_NUM_16;
  static constexpr gpio_num_t greenPin  = GPIO_NUM_17;
  static constexpr gpio_num_t modePin   = GPIO_NUM_0;

  static constexpr int64_t greenMs         = 5000;
  static constexpr int64_t greenBlinkMs    = 3000;
  static constexpr int64_t yellowMs        = 3000;
  static constexpr int64_t redMs           = 5000;
  static constexpr int64_t redYellowMs     = 2000;
  static constexpr int64_t blinkHalfMs     = 500;
  static constexpr int64_t buttonConfirmMs = 50;

  static constexpr TickType_t tickPeriod = pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1;
  static constexpr uint32_t   taskStack  = 4096;
};

enum class Phase : uint8_t { Green, GreenBlink, Yellow, Red, RedYellow, YellowBlink };

namespace {

struct Lights {
  bool red;
  bool yellow;
  bool green;
};

struct PhaseInfo {
  const char* name;
  int64_t     durationMs;
  Lights      lights;
  bool        blink;
  Phase       next;
};

constexpr PhaseInfo phaseTable[] = {
  {"GREEN",        Config::greenMs,      {false, false, true },  false, Phase::GreenBlink},
  {"GREEN BLINK",  Config::greenBlinkMs, {false, false, true },  true,  Phase::Yellow},
  {"YELLOW",       Config::yellowMs,     {false, true,  false},  false, Phase::Red},
  {"RED",          Config::redMs,        {true,  false, false},  false, Phase::RedYellow},
  {"RED + YELLOW", Config::redYellowMs,  {true,  true,  false},  false, Phase::Green},
  {"YELLOW BLINK", 0,                    {false, true,  false},  true,  Phase::YellowBlink},
};

const PhaseInfo& infoOf(Phase phase) {
  return phaseTable[static_cast<size_t>(phase)];
}

int64_t nowMs() {
  return esp_timer_get_time() / 1000;
}

bool isLow(gpio_num_t pin) {
  return gpio_get_level(pin) == 0;
}

void configureOutput(gpio_num_t pin) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&cfg));
  gpio_set_level(pin, 0);
}

void configureButton(gpio_num_t pin) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&cfg));
}

class ButtonFsm {
public:
  bool tick(bool low, int64_t now) {
    switch (state_) {
      case State::Idle:
        if (low) {
          enter(State::Pressed, now);
        }
        break;

      case State::Pressed:
        if (!low) {
          enter(State::Idle, now);
        } else if (now - stateSinceMs_ >= Config::buttonConfirmMs) {
          enter(State::Held, now);
          return true;
        }
        break;

      case State::Held:
        if (!low) {
          enter(State::Release, now);
        }
        break;

      case State::Release:
        if (low) {
          stateSinceMs_ = now;
        } else if (now - stateSinceMs_ >= Config::buttonConfirmMs) {
          enter(State::Idle, now);
        }
        break;
    }
    return false;
  }

private:
  enum class State : uint8_t { Idle, Pressed, Held, Release };

  void enter(State next, int64_t now) {
    state_ = next;
    stateSinceMs_ = now;
  }

  State   state_ = State::Idle;
  int64_t stateSinceMs_ = 0;
};

class TrafficLight {
public:
  void init(int64_t now) {
    configureOutput(Config::redPin);
    configureOutput(Config::yellowPin);
    configureOutput(Config::greenPin);
    enter(Phase::Green, now);
  }

  void update(int64_t now) {
    const PhaseInfo& info = infoOf(phase_);
    const int64_t elapsed = now - phaseStartMs_;

    if (info.durationMs > 0 && elapsed >= info.durationMs) {
      enter(info.next, now);
      return;
    }

    if (info.blink) {
      const bool on = (elapsed / Config::blinkHalfMs) % 2 == 0;
      apply(on ? info.lights : Lights{false, false, false});
    }
  }

  void toggleNightMode(int64_t now) {
    if (phase_ == Phase::YellowBlink) {
      enter(Phase::Red, now);
    } else {
      enter(Phase::YellowBlink, now);
    }
  }

private:
  void enter(Phase next, int64_t now) {
    phase_ = next;
    phaseStartMs_ = now;

    const PhaseInfo& info = infoOf(phase_);
    apply(info.lights);

    if (info.durationMs > 0) {
      printf("[%8.2f s] %-12s for %lld ms\n", now / 1000.0, info.name,
             static_cast<long long>(info.durationMs));
    } else {
      printf("[%8.2f s] %-12s until BOOT is pressed\n", now / 1000.0, info.name);
    }
  }

  void apply(const Lights& lights) {
    gpio_set_level(Config::redPin, lights.red ? 1 : 0);
    gpio_set_level(Config::yellowPin, lights.yellow ? 1 : 0);
    gpio_set_level(Config::greenPin, lights.green ? 1 : 0);
  }

  Phase   phase_ = Phase::Green;
  int64_t phaseStartMs_ = 0;
};

TrafficLight trafficLight;
ButtonFsm    modeButton;

void trafficTask(void*) {
  configureButton(Config::modePin);
  trafficLight.init(nowMs());
  printf("Traffic light started. Press BOOT to toggle YELLOW BLINK mode.\n");

  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    xTaskDelayUntil(&lastWake, Config::tickPeriod);
    const int64_t now = nowMs();

    if (modeButton.tick(isLow(Config::modePin), now)) {
      trafficLight.toggleNightMode(now);
    }
    trafficLight.update(now);
  }
}

}  // namespace

extern "C" void app_main() {
  xTaskCreate(trafficTask, "traffic_light", Config::taskStack, nullptr, 5, nullptr);
}
