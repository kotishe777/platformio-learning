/*
 * Module 3.3 hometask — PWM: one potentiometer controls LED brightness and DC motor speed
 * Board: ESP32-S3 DevKitC-1, framework ESP-IDF 5.5
 * Pot: 3V3 -> 10k pot -> GND, wiper -> GPIO7 (ADC1_CH6)
 * LED:   GPIO15 -> 220R -> LED -> GND, LEDC timer 0, 1 kHz, 10 bit
 * Motor: GPIO16 -> 220R -> 2N2222A base (10k pull-down), emitter -> GND, collector -> motor,
 *        flyback diode, separate 5V supply, common GND, LEDC timer 1, 20 kHz, 10 bit
 * Separate timers, channels, pins and power: LED and motor do not affect each other
 */
#include <cstdint>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct Config {
  static constexpr adc_unit_t       adcUnit        = ADC_UNIT_1;
  static constexpr adc_channel_t    potChannel     = ADC_CHANNEL_6;
  static constexpr adc_atten_t      adcAtten       = ADC_ATTEN_DB_12;
  static constexpr int              adcMax         = 4095;
  static constexpr uint32_t         samplesPerRead = 8;

  static constexpr ledc_mode_t      speedMode      = LEDC_LOW_SPEED_MODE;
  static constexpr uint32_t         resolutionBits = 10;
  static constexpr ledc_timer_bit_t resolution     = static_cast<ledc_timer_bit_t>(resolutionBits);
  static constexpr uint32_t         dutyMax        = (1u << resolutionBits) - 1;

  static constexpr gpio_num_t       ledPin         = GPIO_NUM_15;
  static constexpr ledc_timer_t     ledTimer       = LEDC_TIMER_0;
  static constexpr ledc_channel_t   ledChannel     = LEDC_CHANNEL_0;
  static constexpr uint32_t         ledFreqHz      = 1000;

  static constexpr gpio_num_t       motorPin       = GPIO_NUM_16;
  static constexpr ledc_timer_t     motorTimer     = LEDC_TIMER_1;
  static constexpr ledc_channel_t   motorChannel   = LEDC_CHANNEL_1;
  static constexpr uint32_t         motorFreqHz    = 20000;
  static constexpr int              motorDeadband  = 100;
  static constexpr uint32_t         motorMinDuty   = 300;

  static constexpr uint32_t         dutyHysteresis = 4;
  static constexpr uint32_t         periodMs       = 20;
  static constexpr uint32_t         logEvery       = 10;
};

static_assert(Config::ledFreqHz * (Config::dutyMax + 1) <= 80000000u, "LED PWM: frequency x resolution too high");
static_assert(Config::motorFreqHz * (Config::dutyMax + 1) <= 80000000u, "Motor PWM: frequency x resolution too high");
static_assert(Config::motorMinDuty < Config::dutyMax, "Motor min duty must be below max duty");
static_assert(Config::motorDeadband < Config::adcMax, "Motor deadband must be inside ADC range");
static_assert(Config::ledTimer != Config::motorTimer, "LED and motor must use different timers");

namespace {

constexpr const char* tag = "pwm";

adc_oneshot_unit_handle_t initAdc() {
  adc_oneshot_unit_init_cfg_t unitCfg{};
  unitCfg.unit_id  = Config::adcUnit;
  unitCfg.ulp_mode = ADC_ULP_MODE_DISABLE;

  adc_oneshot_unit_handle_t handle = nullptr;
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitCfg, &handle));

  adc_oneshot_chan_cfg_t chanCfg{};
  chanCfg.atten    = Config::adcAtten;
  chanCfg.bitwidth = ADC_BITWIDTH_12;
  ESP_ERROR_CHECK(adc_oneshot_config_channel(handle, Config::potChannel, &chanCfg));
  return handle;
}

int readAverageRaw(adc_oneshot_unit_handle_t adc) {
  int32_t sum = 0;
  for (uint32_t i = 0; i < Config::samplesPerRead; ++i) {
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc, Config::potChannel, &raw));
    sum += raw;
  }
  return static_cast<int>(sum / static_cast<int32_t>(Config::samplesPerRead));
}

void initPwm(ledc_timer_t timer, uint32_t freqHz, ledc_channel_t channel, gpio_num_t pin) {
  ledc_timer_config_t timerCfg{};
  timerCfg.speed_mode      = Config::speedMode;
  timerCfg.duty_resolution = Config::resolution;
  timerCfg.timer_num       = timer;
  timerCfg.freq_hz         = freqHz;
  timerCfg.clk_cfg         = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&timerCfg));

  ledc_channel_config_t channelCfg{};
  channelCfg.gpio_num   = pin;
  channelCfg.speed_mode = Config::speedMode;
  channelCfg.channel    = channel;
  channelCfg.intr_type  = LEDC_INTR_DISABLE;
  channelCfg.timer_sel  = timer;
  channelCfg.duty       = 0;
  channelCfg.hpoint     = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&channelCfg));
}

uint32_t ledDutyFromRaw(int raw) {
  return static_cast<uint32_t>(raw) * Config::dutyMax / Config::adcMax;
}

uint32_t motorDutyFromRaw(int raw) {
  if (raw < Config::motorDeadband) {
    return 0;
  }
  const uint32_t span = static_cast<uint32_t>(Config::adcMax - Config::motorDeadband);
  const uint32_t pos  = static_cast<uint32_t>(raw - Config::motorDeadband);
  return Config::motorMinDuty + pos * (Config::dutyMax - Config::motorMinDuty) / span;
}

uint32_t percent(uint32_t duty) {
  return duty * 100 / Config::dutyMax;
}

class PwmChannel {
 public:
  explicit PwmChannel(ledc_channel_t channel) : channel_(channel) {}

  void set(uint32_t duty) {
    const uint32_t diff = duty > duty_ ? duty - duty_ : duty_ - duty;
    const bool     edge = duty == 0 || duty == Config::dutyMax;
    if (diff == 0 || (diff < Config::dutyHysteresis && !edge)) {
      return;
    }
    ESP_ERROR_CHECK(ledc_set_duty(Config::speedMode, channel_, duty));
    ESP_ERROR_CHECK(ledc_update_duty(Config::speedMode, channel_));
    duty_ = duty;
  }

  uint32_t duty() const {
    return duty_;
  }

 private:
  ledc_channel_t channel_;
  uint32_t       duty_ = 0;
};

}  // namespace

extern "C" void app_main() {
  const adc_oneshot_unit_handle_t adc = initAdc();
  initPwm(Config::ledTimer, Config::ledFreqHz, Config::ledChannel, Config::ledPin);
  initPwm(Config::motorTimer, Config::motorFreqHz, Config::motorChannel, Config::motorPin);

  PwmChannel led(Config::ledChannel);
  PwmChannel motor(Config::motorChannel);

  ESP_LOGI(tag, "Ready: LED %u Hz, motor %u Hz, %u bit (duty 0..%u)",
           static_cast<unsigned>(Config::ledFreqHz), static_cast<unsigned>(Config::motorFreqHz),
           static_cast<unsigned>(Config::resolutionBits), static_cast<unsigned>(Config::dutyMax));

  uint32_t   tick     = 0;
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    const int raw = readAverageRaw(adc);

    led.set(ledDutyFromRaw(raw));
    motor.set(motorDutyFromRaw(raw));

    if (++tick % Config::logEvery == 0) {
      ESP_LOGI(tag, "pot=%4d | LED duty=%4u (%3u%%) | motor duty=%4u (%3u%%)",
               raw,
               static_cast<unsigned>(led.duty()), static_cast<unsigned>(percent(led.duty())),
               static_cast<unsigned>(motor.duty()), static_cast<unsigned>(percent(motor.duty())));
    }

    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::periodMs));
  }
}