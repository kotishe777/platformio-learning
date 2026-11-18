#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

struct Tone {
  uint16_t freqHz;
  uint16_t durationMs;
};

class Buzzer {
 public:
  Buzzer(gpio_num_t pin, ledc_timer_t timer, ledc_channel_t channel)
      : pin_(pin), timer_(timer), channel_(channel) {}

  void init() {
    ledc_timer_config_t timerCfg{};
    timerCfg.speed_mode      = speedMode;
    timerCfg.timer_num       = timer_;
    timerCfg.duty_resolution = static_cast<ledc_timer_bit_t>(resolutionBits);
    timerCfg.freq_hz         = 1000;
    timerCfg.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timerCfg));

    ledc_channel_config_t channelCfg{};
    channelCfg.gpio_num   = pin_;
    channelCfg.speed_mode = speedMode;
    channelCfg.channel    = channel_;
    channelCfg.intr_type  = LEDC_INTR_DISABLE;
    channelCfg.timer_sel  = timer_;
    channelCfg.duty       = 0;
    channelCfg.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&channelCfg));
  }

  void play(const Tone* tones, size_t count, uint32_t nowMs) {
    tones_ = tones;
    count_ = count;
    index_ = 0;
    if (busy()) {
      startTone(nowMs);
    } else {
      silence();
    }
  }

  void beep(uint16_t freqHz, uint16_t durationMs, uint32_t nowMs) {
    single_ = {freqHz, durationMs};
    play(&single_, 1, nowMs);
  }

  void stop() {
    count_ = 0;
    silence();
  }

  bool busy() const {
    return index_ < count_;
  }

  void update(uint32_t nowMs) {
    if (!busy() || nowMs - startedMs_ < tones_[index_].durationMs) {
      return;
    }
    ++index_;
    if (busy()) {
      startTone(nowMs);
    } else {
      silence();
    }
  }

 private:
  static constexpr ledc_mode_t speedMode      = LEDC_LOW_SPEED_MODE;
  static constexpr uint32_t    resolutionBits = 10;
  static constexpr uint32_t    halfDuty       = (1u << resolutionBits) / 2;

  void startTone(uint32_t nowMs) {
    startedMs_ = nowMs;
    const Tone& tone = tones_[index_];
    if (tone.freqHz == 0) {
      silence();
      return;
    }
    ESP_ERROR_CHECK(ledc_set_freq(speedMode, timer_, tone.freqHz));
    ESP_ERROR_CHECK(ledc_set_duty(speedMode, channel_, halfDuty));
    ESP_ERROR_CHECK(ledc_update_duty(speedMode, channel_));
  }

  void silence() {
    ESP_ERROR_CHECK(ledc_set_duty(speedMode, channel_, 0));
    ESP_ERROR_CHECK(ledc_update_duty(speedMode, channel_));
  }

  gpio_num_t     pin_;
  ledc_timer_t   timer_;
  ledc_channel_t channel_;
  const Tone*    tones_     = nullptr;
  size_t         count_     = 0;
  size_t         index_     = 0;
  uint32_t       startedMs_ = 0;
  Tone           single_    = {0, 0};
};
