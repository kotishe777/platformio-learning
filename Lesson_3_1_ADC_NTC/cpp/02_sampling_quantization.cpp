/**
 * 02_sampling_quantization.cpp
 * Модуль 3.1 • ESP32-S3 • ESP-IDF 5.5.x • C++
 * Два досліди: період читання та програмне огрублення 12-бітного результату до 9 біт.
 *
 * Самостійний приклад: у main/CMakeLists.txt виберіть лише цей .cpp.
 * Статус: вихідний текст перевірено читанням; НЕ компілювався, на платі не запускався.
 * Повна схема, формули та досліди: ../Lesson_3_1_ADC_NTC_UA.html
 */


#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char kTag[] = "ADC31";
constexpr adc_channel_t kChannel = ADC_CHANNEL_3; // GPIO4 на ESP32-S3
// Для досліду змінюйте на 20, 200 або 1000 мс і повторюйте той самий рух.
constexpr int kPeriodMs = 20;
constexpr int kDiscardedBits = 3; // 12 - 9: відкидаємо три молодші біти

} // namespace

extern "C" void app_main()
{
    // handle — дескриптор драйвера. Створюємо ADC1 один раз перед циклом.
    adc_oneshot_unit_handle_t adc = nullptr;
    adc_oneshot_unit_init_cfg_t unit_config{};
    unit_config.unit_id = ADC_UNIT_1;
    unit_config.ulp_mode = ADC_ULP_MODE_DISABLE;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc));

    // На ESP32-S3 GPIO4 = ADC1, channel 3.
    adc_oneshot_chan_cfg_t channel_config{};
    channel_config.atten = ADC_ATTEN_DB_12;
    channel_config.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, kChannel, &channel_config));
    
    const TickType_t period_ticks = pdMS_TO_TICKS(kPeriodMs);
    configASSERT(period_ticks > 0);
    TickType_t last_wake = xTaskGetTickCount();
    int64_t previous_us = 0;
    while (true) {
        int raw = 0;
        const esp_err_t read_error = adc_oneshot_read(adc, kChannel, &raw);
        const int64_t now_us = esp_timer_get_time();
        if (read_error == ESP_OK) {
            // ADC залишається 12-бітним. Це НАВЧАЛЬНА програмна імітація 9 біт.
            const int raw9 = raw >> kDiscardedBits;       // 0..511
            const int coarse12 = raw9 << kDiscardedBits; // 0,8,16,...4088
            // coarse12 повертає грубий код у ту саму шкалу, щоб порівнювати сходинки.
            const double dt_ms = previous_us == 0 ? 0.0 :
                static_cast<double>(now_us - previous_us) / 1000.0;
            ESP_LOGI(kTag, "raw12=%d raw9=%d coarse12=%d dt_ms=%.2f",
                     raw, raw9, coarse12, dt_ms);
            previous_us = now_us;
        } else {
            ESP_LOGW(kTag, "ADC read failed: %s", esp_err_to_name(read_error));
        }
        vTaskDelayUntil(&last_wake, period_ticks);
    }
}
