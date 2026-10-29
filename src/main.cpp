/*
 * Module 3.1 hometask — LDR light sensor with SMA filter and hysteresis
 * Board: ESP32-S3 DevKitC-1, framework ESP-IDF
 * LDR divider: 3V3 -> LDR -> GPIO7 (ADC1_CH6) -> 10k -> GND
 * LED: GPIO15 -> 330R -> LED -> GND
 * ADC oneshot read every 20 ms, SMA over 16 samples (320 ms window).
 * Dark: avg < 1000 mV -> LED on. Light: avg > 1400 mV -> LED off.
 * Between thresholds the LED keeps its state (hysteresis, no flicker).
 */
#include <cstdint>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct Config {
  static constexpr adc_unit_t    adcUnit        = ADC_UNIT_1;
  static constexpr adc_channel_t adcChannel     = ADC_CHANNEL_6;
  static constexpr adc_atten_t   adcAtten       = ADC_ATTEN_DB_12;
  static constexpr gpio_num_t    ledPin         = GPIO_NUM_15;
  static constexpr uint32_t      windowSize     = 16;
  static constexpr uint32_t      samplePeriodMs = 20;
  static constexpr uint32_t      logPeriodMs    = 500;
  static constexpr uint32_t      logEvery       = logPeriodMs / samplePeriodMs;
  static constexpr int           darkMv         = 1000;
  static constexpr int           lightMv        = 1400;
  static constexpr int           midMv          = (darkMv + lightMv) / 2;
};

static_assert(Config::windowSize > 0, "SMA window must not be empty");
static_assert(Config::darkMv < Config::lightMv, "Dark threshold must be below light threshold");
static_assert(Config::logEvery > 0, "Log period must be at least one sample period");

namespace {

constexpr const char* tag = "sma";

enum class LightState : uint8_t { Unknown, Dark, Light };

class MovingAverage {
 public:
  int add(int sample) {
    sum_            -= buffer_[index_];
    buffer_[index_]  = sample;
    sum_            += sample;
    index_           = (index_ + 1) % Config::windowSize;
    if (count_ < Config::windowSize) {
      ++count_;
    }
    return static_cast<int>(sum_ / static_cast<int32_t>(count_));
  }

  bool full() const {
    return count_ == Config::windowSize;
  }

 private:
  int      buffer_[Config::windowSize]{};
  int32_t  sum_   = 0;
  uint32_t index_ = 0;
  uint32_t count_ = 0;
};

adc_oneshot_unit_handle_t initAdc() {
  adc_oneshot_unit_init_cfg_t unitCfg{};
  unitCfg.unit_id  = Config::adcUnit;
  unitCfg.ulp_mode = ADC_ULP_MODE_DISABLE;

  adc_oneshot_unit_handle_t handle = nullptr;
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitCfg, &handle));

  adc_oneshot_chan_cfg_t chanCfg{};
  chanCfg.atten    = Config::adcAtten;
  chanCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
  ESP_ERROR_CHECK(adc_oneshot_config_channel(handle, Config::adcChannel, &chanCfg));
  return handle;
}

adc_cali_handle_t initCalibration() {
  adc_cali_curve_fitting_config_t caliCfg{};
  caliCfg.unit_id  = Config::adcUnit;
  caliCfg.chan     = Config::adcChannel;
  caliCfg.atten    = Config::adcAtten;
  caliCfg.bitwidth = ADC_BITWIDTH_DEFAULT;

  adc_cali_handle_t handle = nullptr;
  ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&caliCfg, &handle));
  return handle;
}

void initLed() {
  gpio_config_t io{};
  io.pin_bit_mask = 1ULL << Config::ledPin;
  io.mode         = GPIO_MODE_OUTPUT;
  io.pull_up_en   = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type    = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&io));
  gpio_set_level(Config::ledPin, 0);
}

LightState nextState(LightState current, int avgMv) {
  if (avgMv < Config::darkMv) {
    return LightState::Dark;
  }
  if (avgMv > Config::lightMv) {
    return LightState::Light;
  }
  if (current == LightState::Unknown) {
    return avgMv < Config::midMv ? LightState::Dark : LightState::Light;
  }
  return current;
}

const char* toString(LightState state) {
  switch (state) {
    case LightState::Dark:  return "DARK";
    case LightState::Light: return "LIGHT";
    default:                return "UNKNOWN";
  }
}

}  // namespace

extern "C" void app_main() {
  const adc_oneshot_unit_handle_t adc  = initAdc();
  const adc_cali_handle_t         cali = initCalibration();
  initLed();

  ESP_LOGI(tag, "Ready: dark < %d mV, light > %d mV, window %u samples",
           Config::darkMv, Config::lightMv, static_cast<unsigned>(Config::windowSize));

  MovingAverage sma;
  LightState    state       = LightState::Unknown;
  uint32_t      sampleCount = 0;
  TickType_t    lastWake    = xTaskGetTickCount();

  while (true) {
    int raw = 0;
    int mv  = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc, Config::adcChannel, &raw));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali, raw, &mv));

    const int avg = sma.add(mv);
    ++sampleCount;

    if (sma.full()) {
      const LightState next = nextState(state, avg);
      if (next != state) {
        state = next;
        gpio_set_level(Config::ledPin, state == LightState::Dark ? 1 : 0);
        ESP_LOGI(tag, "-> %s (avg=%d mV), LED %s",
                 toString(state), avg, state == LightState::Dark ? "ON" : "OFF");
      }
    }

    if (sampleCount % Config::logEvery == 0) {
      ESP_LOGI(tag, "raw=%4d mv=%4d avg=%4d state=%s",
               raw, mv, avg, toString(state));
    }

    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::samplePeriodMs));
  }
}