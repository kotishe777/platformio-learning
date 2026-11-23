#pragma once

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

class LightSensor {
 public:
  LightSensor(adc_unit_t unit, adc_channel_t channel) : unit_(unit), channel_(channel) {}

  void init() {
    adc_oneshot_unit_init_cfg_t unitCfg{};
    unitCfg.unit_id  = unit_;
    unitCfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitCfg, &adc_));

    adc_oneshot_chan_cfg_t chanCfg{};
    chanCfg.atten    = atten;
    chanCfg.bitwidth = bitwidth;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_, channel_, &chanCfg));

    adc_cali_curve_fitting_config_t caliCfg{};
    caliCfg.unit_id  = unit_;
    caliCfg.chan     = channel_;
    caliCfg.atten    = atten;
    caliCfg.bitwidth = bitwidth;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&caliCfg, &cali_));
  }

  int readMv() {
    int raw = 0;
    int mv  = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_, channel_, &raw));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_, raw, &mv));
    return mv;
  }

 private:
  static constexpr adc_atten_t    atten    = ADC_ATTEN_DB_12;
  static constexpr adc_bitwidth_t bitwidth = ADC_BITWIDTH_12;

  adc_unit_t                unit_;
  adc_channel_t             channel_;
  adc_oneshot_unit_handle_t adc_  = nullptr;
  adc_cali_handle_t         cali_ = nullptr;
};
