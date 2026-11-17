#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "range.hpp"

struct ServoConfig {
  gpio_num_t     pin;
  ledc_timer_t   timer;
  ledc_channel_t channel;
  int            minDeg;
  int            maxDeg;
  int            pulse0Us;
  int            pulse180Us;
};

class Servo {
 public:
  explicit Servo(const ServoConfig& config) : config_(config) {}

  void init(int startDeg) {
    ledc_timer_config_t timer{};
    timer.speed_mode      = speedMode;
    timer.timer_num       = config_.timer;
    timer.duty_resolution = static_cast<ledc_timer_bit_t>(resolutionBits);
    timer.freq_hz         = freqHz;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    angle_ = clampAngle(startDeg);
    ledc_channel_config_t channel{};
    channel.gpio_num   = config_.pin;
    channel.speed_mode = speedMode;
    channel.channel    = config_.channel;
    channel.intr_type  = LEDC_INTR_DISABLE;
    channel.timer_sel  = config_.timer;
    channel.duty       = dutyForAngle(angle_);
    channel.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
  }

  int setAngle(int deg) {
    angle_ = clampAngle(deg);
    ESP_ERROR_CHECK(ledc_set_duty(speedMode, config_.channel, dutyForAngle(angle_)));
    ESP_ERROR_CHECK(ledc_update_duty(speedMode, config_.channel));
    return angle_;
  }

  int angle() const {
    return angle_;
  }

  int minDeg() const {
    return config_.minDeg;
  }

  int maxDeg() const {
    return config_.maxDeg;
  }

 private:
  static constexpr ledc_mode_t speedMode      = LEDC_LOW_SPEED_MODE;
  static constexpr uint32_t    freqHz         = 50;
  static constexpr uint32_t    periodUs       = 1000000 / freqHz;
  static constexpr uint32_t    resolutionBits = 14;
  static constexpr uint32_t    dutySteps      = 1u << resolutionBits;
  static constexpr int         fullRangeDeg   = 180;

  int clampAngle(int deg) const {
    return clampValue(deg, config_.minDeg, config_.maxDeg);
  }

  uint32_t dutyForAngle(int deg) const {
    const int pulseUs = mapRange(deg, 0, fullRangeDeg, config_.pulse0Us, config_.pulse180Us);
    return static_cast<uint32_t>(pulseUs) * dutySteps / periodUs;
  }

  ServoConfig config_;
  int         angle_ = 0;
};
