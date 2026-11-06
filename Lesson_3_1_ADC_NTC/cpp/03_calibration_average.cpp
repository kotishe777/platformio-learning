/**
 * 03_calibration_average.cpp
 * Модуль 3.1 • ESP32-S3 • ESP-IDF 5.5.x • C++
 * Порівняння raw, умовної формули 3.3 В, каліброваних мВ і середнього 16 відліків.
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
constexpr float kNaiveFullScaleMv = 3300.0f; // навмисне спрощення для ПОРІВНЯННЯ

} // namespace

extern "C" void app_main()
{
    // handle — дескриптор драйвера. Створюємо ADC1 один раз перед циклом.
    adc_oneshot_unit_handle_t adc = nullptr;
    adc_oneshot_unit_init_cfg_t unit_config{};
    unit_config.unit_id = ADC_UNIT_1;
    unit_config.ulp_mode = ADC_ULP_MODE_DISABLE;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc));

    // На ESP32-S3 GPIO4 = ADC1, channel 3
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
    int count = 0;
    int sum_mv = 0;
    int min_mv = 0;
    int max_mv = 0;
    while (true) {
        int raw = 0;
        const esp_err_t read_error = adc_oneshot_read(adc, kChannel, &raw);
        int mv = 0;
        const esp_err_t conversion_error = read_error == ESP_OK ?
            adc_cali_raw_to_voltage(calibration, raw, &mv) : read_error;
        if (conversion_error != ESP_OK) {
            ESP_LOGW(kTag, "Measurement failed: %s", esp_err_to_name(conversion_error));
            count = 0; // новий блок має складатися з послідовних успішних читань
            sum_mv = 0;
        } else {
            if (count == 0) { min_mv = mv; max_mv = mv; }
            min_mv = std::min(min_mv, mv);
            max_mv = std::max(max_mv, mv);
            sum_mv += mv; // спочатку калібруємо КОЖЕН код, потім усереднюємо мВ
            ++count;
            if (count == kSamples) {
                const float average_mv = static_cast<float>(sum_mv) / kSamples;
                const float naive_mv = static_cast<float>(raw) * kNaiveFullScaleMv / 4095.0f;
                // raw/naive/last_mv стосуються останнього відліку; mean — усього блоку.
                ESP_LOGI(kTag,
                    "raw=%d naive_mv=%.1f last_mv=%d mean_mv=%.1f min=%d max=%d pp=%d",
                    raw, naive_mv, mv, average_mv, min_mv, max_mv, max_mv - min_mv);
                count = 0;
                sum_mv = 0;
            }
        }
        // Один відлік за ітерацію. Повідомлення приблизно раз на 16*20=320 мс.
        vTaskDelayUntil(&last_wake, period_ticks);
    }
    // При завершенні: adc_cali_delete_scheme_curve_fitting(), adc_oneshot_del_unit().
}
