/*
 * Module 3.2 hometask — ADC calibration: manual formula vs calibrated driver
 * Board: ESP32-S3 DevKitC-1, framework ESP-IDF 5.5
 * Potentiometer 10k: 3V3 -> pot -> GND, wiper -> GPIO7 (ADC1_CH6)
 * ADC oneshot, 12 bit, attenuation 12 dB, read every 100 ms (average of 8)
 * U_manual = RAW * 3300 / 4095 mV, U_cali = adc_cali_raw_to_voltage()
 * Error(%) = (U_manual - U_cali) / U_cali * 100
 * Output: table in serial console, header repeated every 20 rows
 */
#include <cstdint>
#include <cstdio>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct Config {
  static constexpr adc_unit_t     adcUnit        = ADC_UNIT_1;
  static constexpr adc_channel_t  adcChannel     = ADC_CHANNEL_6;
  static constexpr adc_atten_t    adcAtten       = ADC_ATTEN_DB_12;
  static constexpr adc_bitwidth_t adcBitwidth    = ADC_BITWIDTH_12;
  static constexpr uint32_t       vrefNominalMv  = 1100;
  static constexpr float          vmaxMv         = 3300.0f;
  static constexpr int            dmax           = 4095;
  static constexpr uint32_t       samplesPerRead = 8;
  static constexpr uint32_t       periodMs       = 100;
  static constexpr uint32_t       rowsPerHeader  = 20;
};

static_assert(Config::samplesPerRead > 0, "At least one sample per reading");
static_assert(Config::rowsPerHeader > 0, "Header period must not be zero");

namespace {

adc_oneshot_unit_handle_t initAdc() {
  adc_oneshot_unit_init_cfg_t unitCfg{};
  unitCfg.unit_id  = Config::adcUnit;
  unitCfg.ulp_mode = ADC_ULP_MODE_DISABLE;

  adc_oneshot_unit_handle_t handle = nullptr;
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitCfg, &handle));

  adc_oneshot_chan_cfg_t chanCfg{};
  chanCfg.atten    = Config::adcAtten;
  chanCfg.bitwidth = Config::adcBitwidth;
  ESP_ERROR_CHECK(adc_oneshot_config_channel(handle, Config::adcChannel, &chanCfg));
  return handle;
}

adc_cali_handle_t initCalibration() {
  adc_cali_curve_fitting_config_t caliCfg{};
  caliCfg.unit_id  = Config::adcUnit;
  caliCfg.chan     = Config::adcChannel;
  caliCfg.atten    = Config::adcAtten;
  caliCfg.bitwidth = Config::adcBitwidth;

  adc_cali_handle_t handle = nullptr;
  ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&caliCfg, &handle));
  return handle;
}

int readAverageRaw(adc_oneshot_unit_handle_t adc) {
  int32_t sum = 0;
  for (uint32_t i = 0; i < Config::samplesPerRead; ++i) {
    int raw = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc, Config::adcChannel, &raw));
    sum += raw;
  }
  return static_cast<int>(sum / static_cast<int32_t>(Config::samplesPerRead));
}

float manualMv(int raw) {
  return static_cast<float>(raw) * Config::vmaxMv / static_cast<float>(Config::dmax);
}

void printSettings() {
  printf("\nADC settings\n");
  printf("  Unit / channel : ADC1 / CH6 (GPIO7)\n");
  printf("  Resolution     : 12 bit, RAW 0..%d\n", Config::dmax);
  printf("  Attenuation    : 12 dB, about 0..3100 mV\n");
  printf("  Vref nominal   : %u mV\n", static_cast<unsigned>(Config::vrefNominalMv));
  printf("  Manual formula : U = RAW * %.0f / %d mV\n", Config::vmaxMv, Config::dmax);
  printf("  Manual LSB     : %.3f mV\n", Config::vmaxMv / (Config::dmax + 1));
  printf("  Calibration    : curve fitting (eFuse data)\n\n");
}

void printHeader() {
  printf("%5s %14s %12s %10s\n", "RAW", "U_manual(mV)", "U_cali(mV)", "Error(%)");
  printf("--------------------------------------------\n");
}

}  // namespace

extern "C" void app_main() {
  const adc_oneshot_unit_handle_t adc  = initAdc();
  const adc_cali_handle_t         cali = initCalibration();

  vTaskDelay(pdMS_TO_TICKS(2000));
  printSettings();

  uint32_t   row      = 0;
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    if (row % Config::rowsPerHeader == 0) {
      printHeader();
    }

    const int raw    = readAverageRaw(adc);
    int       caliMv = 0;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali, raw, &caliMv));
    const float manual = manualMv(raw);

    if (caliMv > 0) {
      const float error = (manual - caliMv) / caliMv * 100.0f;
      printf("%5d %14.1f %12d %10.2f\n", raw, manual, caliMv, error);
    } else {
      printf("%5d %14.1f %12d %10s\n", raw, manual, caliMv, "n/a");
    }

    ++row;
    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::periodMs));
  }
}