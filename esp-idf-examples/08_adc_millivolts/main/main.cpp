/*
 * Lesson 2.6 — ESP-IDF example 08: ADC oneshot read with calibration (mV).
 * Board: ESP32-S3 DevKitC-1 (N16R8), ESP-IDF 5.5 via PlatformIO (espressif32@6.12.0).
 * Potentiometer wiper on GPIO1 (ADC7_CH0), 12-bit, attenuation 12 dB.
 * Once per second logs RAW code (0..4095) and calibrated millivolts
 * (curve fitting with factory eFuse data).
 *
 * ============================================================================
 * СХЕМА ПІДКЛЮЧЕННЯ
 * ============================================================================
 *   Потенціометр B10K (валом до себе, ніжками вниз):
 *   1. Ліва ніжка                -> GND
 *   2. Середня ніжка (повзунок)  -> ESP32 GPIO7 (ADC7_CH0)
 *   3. Права ніжка               -> ESP32 3V3   (НЕ 5V!)
 *   4. ESP32 USB                 -> комп'ютер
 *
 *   3V3 ---- права ---┐
 *                    [▓]<-- середня ---- GPIO7
 *   GND ---- ліва  ---┘
 *
 * ============================================================================
 * ЩО ПЕРЕВІРИТИ
 * ============================================================================
 *   - Лог щосекунди: "raw=..., calibrated=... mV".
 *   - Ручка в мінімумі -> raw ≈ 0,    ≈ 0 mV.
 *   - Ручка посередині -> raw ≈ 2000, ≈ 1600–1700 mV.
 *   - Ручка в максимумі -> raw = 4095, ≈ 3100+ mV (насичення АЦП, це нормально).
 *   - Обертання -> числа змінюються плавно.
 *
 * RAW — номер поділки «лінійки» АЦП (12 біт = 4096 поділок), а не мілівольти.
 * Калібровка враховує нелінійність і похибку саме цього чипа (дані в eFuse),
 * тому raw * 3300 / 4095 — лише грубе наближення.
 *
 * Arduino -> ESP-IDF:
 *   analogRead()           -> adc_oneshot_read()
 *   analogReadMilliVolts() -> adc_oneshot_read() + adc_cali_raw_to_voltage()
 *   Serial.printf()        -> ESP_LOGI(tag, ...)
 * 
 * ~/.platformio/penv/bin/pio run -d esp-idf-examples/08_adc_millivolts -t upload -t monitor
 */

#include <cstdint>
#include <cinttypes>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

// constexpr — константа часу компіляції; імена GPIO та enum беруться з ESP-IDF.
// ESP_LOGI(tag, format, ...) друкує лог Info з тегом джерела, на відміну від Serial.print.
constexpr char log_tag[] = "adc_calibration";
// extern "C" зберігає ім'я app_main для стартового C-коду ESP-IDF.
// app_main уже виконується у FreeRTOS main task; setup()/loop() тут немає.
// ESP_ERROR_CHECK(expr) перевіряє esp_err_t: ESP_OK — успіх, інакше діагностика й abort().
extern "C" void app_main()
{
    // Крок 1: ADC unit — апаратний блок; channel — його конкретний аналоговий вхід.
    // На ESP32-S3 GPIO1 відповідає ADC1_CH0. {} обнуляє необов'язкові поля.
    adc_oneshot_unit_init_cfg_t unit_config{};
    unit_config.unit_id = ADC_UNIT_1;
    unit_config.ulp_mode = ADC_ULP_MODE_DISABLE; // ADC керує основний CPU, не ULP.
    adc_oneshot_unit_handle_t adc_handle = nullptr;
    // new_unit(конфігурація, адреса handle) створює ресурс і повертає esp_err_t.
    // Handle — дескриптор ресурсу драйвера; він потрібен наступним викликам.
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));
    // Крок 2: конфігуруємо канал; attenuation — ослаблення сигналу перед ADC.
    // Це розширює вимірюваний діапазон, але не робить його рівним 3.3 V.
    adc_oneshot_chan_cfg_t adc_channel_config{};
    adc_channel_config.bitwidth = ADC_BITWIDTH_12; // 12 біт: ідеальні коди 0..4095.
    adc_channel_config.atten = ADC_ATTEN_DB_12;
    // config_channel(handle, channel, конфігурація): канал має належати цьому unit.
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_6, &adc_channel_config));
    // Крок 3: схема калібрування має відповідати unit/channel/atten/bitwidth читання.
    adc_cali_handle_t calibration_handle = nullptr;
    esp_err_t calibration_result = ESP_ERR_NOT_SUPPORTED;
    // #if перевіряє підтримку драйвером при компіляції; це не runtime-перевірка MCU.
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t calibration_config{};
    calibration_config.unit_id = ADC_UNIT_1;
    calibration_config.chan = ADC_CHANNEL_6;
    calibration_config.atten = ADC_ATTEN_DB_12;
    calibration_config.bitwidth = ADC_BITWIDTH_12;
    // create_scheme(config, &handle) використовує калібрувальні дані eFuse;
    // ESP_ERR_NOT_SUPPORTED означає, що потрібна підтримка/дані недоступні.
    calibration_result = adc_cali_create_scheme_curve_fitting(&calibration_config, &calibration_handle);
#endif
    if (calibration_result == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(log_tag, "Calibration unavailable: reporting RAW only");
    } else {
        ESP_ERROR_CHECK(calibration_result);
    }
    while (true) {
        // adc_oneshot_read(handle, channel, &raw) виконує одне перетворення ADC.
        // Повертає esp_err_t, а RAW пише через третій аргумент int*, на відміну від analogRead().
        int adc_raw = 0; // int потрібен саме для вихідного параметра драйвера.
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_6, &adc_raw));
        if (calibration_result == ESP_OK) {
            int voltage_mv = 0; // int* — тип вихідного параметра calibration API.
            // raw_to_voltage(handle, raw, &mV) повертає esp_err_t й записує калібровані mV.
            // Це роль analogReadMilliVolts(), але читання і калібрування тут розділені.
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(calibration_handle, adc_raw, &voltage_mv));
            ESP_LOGI(log_tag, "raw=%d, calibrated=%d mV", adc_raw, voltage_mv);
        } else {
            ESP_LOGI(log_tag, "raw=%d, calibrated mV unavailable", adc_raw);
        }
        // vTaskDelay приймає ticks; pdMS_TO_TICKS переводить ms за частотою RTOS.
        // На відміну від delay(ms), одиниця аргументу тут — tick; інші задачі можуть працювати.
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
