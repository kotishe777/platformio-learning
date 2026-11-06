#include <stdio.h>              // стандартна бібліотека C
#include "freertos/FreeRTOS.h"  // базові типи й макроси FreeRTOS (потрібен для vTaskDelay, pdMS_TO_TICKS)
#include "freertos/task.h"      // функції керування задачами: vTaskDelay і т.п.
#include "esp_adc/adc_oneshot.h" // новий (IDF v5.x) API одноразових замірів АЦП
#include "esp_log.h"            // макрос ESP_LOGI() для виводу в UART-монітор

#define ADC_UNIT     ADC_UNIT_1        // АЦП1 — не конфліктує з Wi-Fi
#define ADC_CHANNEL  ADC_CHANNEL_6     // GPIO34 на класичному ESP32 відповідає ADC1_CHANNEL_6
static const char *TAG = "ADC_LAB";    // "тег" логів — буде показуватись перед кожним повідомленням у моніторі

void app_main(void)              // точка входу програми на ESP-IDF (аналог setup()+loop() в Arduino, але одна функція)
{
    adc_oneshot_unit_handle_t adc1_handle;                 // хендл (дескриптор) блока АЦП1 — потрібен для всіх подальших викликів
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,                               // який саме блок АЦП ініціалізуємо
    };
    adc_oneshot_new_unit(&init_config, &adc1_handle);      // створюємо (реєструємо) блок АЦП1 і отримуємо хендл

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,                  // роздільна здатність — типова для чипа (12 біт на класичному ESP32)
        .atten    = ADC_ATTEN_DB_12,                       // послаблення входу — дозволяє читати повний діапазон ~0-3.3В (детально — Частина 3)
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &chan_config); // прив'язуємо налаштування до конкретного каналу (піна)

    while (1) {                                            // нескінченний цикл — аналог loop() в Arduino, виконується постійно
        int raw = 0;                                       // сюди прийде "сире" число АЦП
        adc_oneshot_read(adc1_handle, ADC_CHANNEL, &raw);  // ОДИН замір: дискретизація + квантування + кодування за один виклик
        ESP_LOGI(TAG, "Сире значення: %d", raw);           // друкуємо в монітор

        vTaskDelay(pdMS_TO_TICKS(200));                    // пауза 200 мс — щоб монітор встигав, і задача не займала весь CPU
    }
}
