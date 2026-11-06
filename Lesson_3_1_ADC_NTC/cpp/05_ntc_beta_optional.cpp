/**
 * 05_ntc_beta_optional.cpp
 * Модуль 3.1 • ESP32-S3 • ESP-IDF 5.5.x • C++
 * Додатковий дослід: температура за B-моделлю. Схема та вимірювання як у прикладі 04.
 *
 * Самостійний приклад: у main/CMakeLists.txt виберіть лише цей .cpp.
 * Статус: вихідний текст перевірено читанням; НЕ компілювався, на платі не запускався.
 * Повна схема, формули та досліди: ../Lesson_3_1_ADC_NTC_UA.html
 */


#include <algorithm>
#include <cmath>
#include <cstdint>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char kTag[] = "ADC31";
constexpr adc_channel_t kChannel = ADC_CHANNEL_3; // GPIO4 на ESP32-S3
constexpr int kPeriodMs = 20;
constexpr int kSamples = 16;
constexpr float kSupplyMv = 3300.0f; // замініть виміряною напругою 3V3↔GND
constexpr float kFixedOhm = 10000.0f; // верхній постійний резистор; можна уточнити мультиметром
constexpr float kMinUsableMv = 100.0f; // навчальне вікно; не специфікація точності ADC
constexpr float kMaxUsableMv = 3000.0f; // залишаємо запас до верхньої межі вимірювання
constexpr float kR0Ohm = 10000.0f;  // опір при 25 °C, а не за будь-якої температури
constexpr float kT0Kelvin = 298.15f; // 25 + 273.15
constexpr float kBetaKelvin = 3950.0f; // УТОЧНІТЬ для вашого NTC; 10 кОм не визначає B

// Схема: 3V3 → R_fixed → GPIO4 → NTC → GND. NTC — НИЖНЄ плече!
// Зростання температури → опір NTC падає → напруга вузла падає.
float voltage_to_resistance(float mv)
{
    if (!std::isfinite(mv) || mv <= 0.0f || mv >= kSupplyMv) {
        return NAN; // не дозволяємо ділення на нуль і нефізичний результат
    }
    return kFixedOhm * mv / (kSupplyMv - mv);
}

// B-модель — наближення, відмінне від повного рівняння Steinhart–Hart.
// B та R0 беруть із паспорта конкретного NTC. 3950 K — навчальне припущення.
float resistance_to_celsius(float resistance)
{
    if (!std::isfinite(resistance) || resistance <= 0.0f) { return NAN; }
    const float inverse_kelvin = 1.0f / kT0Kelvin +
        std::log(resistance / kR0Ohm) / kBetaKelvin; // натуральний логарифм!
    if (!std::isfinite(inverse_kelvin) || inverse_kelvin <= 0.0f) { return NAN; }
    return 1.0f / inverse_kelvin - 273.15f;
}
} // namespace

extern "C" void app_main()
{
    // handle — дескриптор драйвера. Створюємо ADC1 один раз перед циклом.
    adc_oneshot_unit_handle_t adc = nullptr;
    adc_oneshot_unit_init_cfg_t unit_config{};
    unit_config.unit_id = ADC_UNIT_1;
    unit_config.ulp_mode = ADC_ULP_MODE_DISABLE;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc));

    // На ESP32-S3 GPIO4 = ADC1, channel 3. Номер GPIO ≠ номер каналу!
    adc_oneshot_chan_cfg_t channel_config{};
    channel_config.atten = ADC_ATTEN_DB_12;
    channel_config.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, kChannel, &channel_config));

    // Калібрування має відповідати ADC, каналу, атенюації та розрядності.
    // Для S3 використовуємо Curve Fitting без обгорток і fallback для інших чипів.
    adc_cali_handle_t calibration = nullptr;
    adc_cali_curve_fitting_config_t cal_config{};
    cal_config.unit_id = ADC_UNIT_1;
    cal_config.chan = kChannel;
    cal_config.atten = channel_config.atten;
    cal_config.bitwidth = channel_config.bitwidth;
    const esp_err_t cal_error =
        adc_cali_create_scheme_curve_fitting(&cal_config, &calibration);
    if (cal_error != ESP_OK) {
        // Не видаємо приблизні вольти за калібрований результат.
        ESP_LOGE(kTag, "Calibration failed: %s", esp_err_to_name(cal_error));
        ESP_ERROR_CHECK(adc_oneshot_del_unit(adc));
        return;
    }
    // app_main вже виконується в задачі ESP-IDF. Нових задач не створюємо.
    // Це планування в тиках RTOS, а не точний апаратний тригер ADC.
    const TickType_t period_ticks = pdMS_TO_TICKS(kPeriodMs);
    configASSERT(period_ticks > 0);
    TickType_t last_wake = xTaskGetTickCount();
    float sum_mv = 0.0f;
    int count = 0;
    while (true) {
        int raw = 0;
        const esp_err_t read_error = adc_oneshot_read(adc, kChannel, &raw);
        int mv = 0;
        const esp_err_t measurement_error = read_error == ESP_OK ?
            adc_cali_raw_to_voltage(calibration, raw, &mv) : read_error;
        // Крайні значення не оголошуємо температурою. Причиною може бути
        // обрив, коротке замикання, насичення ADC або вихід за навчальний діапазон.
        const bool valid = measurement_error == ESP_OK &&
            raw > 0 && raw < 4095 &&
            mv > kMinUsableMv && mv < kMaxUsableMv && mv < kSupplyMv;
        if (!valid) {
            ESP_LOGW(kTag, "Invalid/out of lesson range: err=%s raw=%d mv=%d",
                     esp_err_to_name(measurement_error), raw, mv);
            sum_mv = 0.0f;
            count = 0;
        } else {
            sum_mv += static_cast<float>(mv);
            ++count;
            if (count == kSamples) {
                const float mean_mv = sum_mv / kSamples;
                const float resistance = voltage_to_resistance(mean_mv);
                const float temperature = resistance_to_celsius(resistance);
                if (std::isfinite(temperature)) {
                    // Десяті частки в логу — формат, а не гарантія точності ±0.1 °C.
                    ESP_LOGI(kTag, "mean_mv=%.1f R_ntc_ohm=%.1f T_est_C=%.1f",
                             mean_mv, resistance, temperature);
                } else {
                    ESP_LOGW(kTag, "Cannot estimate temperature");
                }
                sum_mv = 0.0f;
                count = 0;
            }
        }
        vTaskDelayUntil(&last_wake, period_ticks);
    }
}
