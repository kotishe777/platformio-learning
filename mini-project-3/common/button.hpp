#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

enum class ButtonEvent : uint8_t { None, Short, Long };

class Button {
 public:
  explicit Button(gpio_num_t pin) : pin_(pin) {}

  void init() {
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << pin_;
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));
  }

  ButtonEvent update(uint32_t nowMs) {
    const bool raw = gpio_get_level(pin_) == 0;
    if (raw != rawPressed_) {
      rawPressed_ = raw;
      changedMs_  = nowMs;
    }
    if (nowMs - changedMs_ < debounceMs) {
      return ButtonEvent::None;
    }
    if (rawPressed_ != stablePressed_) {
      stablePressed_ = rawPressed_;
      if (stablePressed_) {
        pressedMs_ = nowMs;
        longFired_ = false;
        return ButtonEvent::None;
      }
      return longFired_ ? ButtonEvent::None : ButtonEvent::Short;
    }
    if (stablePressed_ && !longFired_ && nowMs - pressedMs_ >= longPressMs) {
      longFired_ = true;
      return ButtonEvent::Long;
    }
    return ButtonEvent::None;
  }

 private:
  static constexpr uint32_t debounceMs  = 30;
  static constexpr uint32_t longPressMs = 1000;

  gpio_num_t pin_;
  bool       rawPressed_    = false;
  bool       stablePressed_ = false;
  bool       longFired_     = false;
  uint32_t   changedMs_     = 0;
  uint32_t   pressedMs_     = 0;
};
