#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define ADC_UNIT     ADC_UNIT_1
#define ADC_CHANNEL  ADC_CHANNEL_6

static const char *TAG = "ADC_LAB";

void app_main(void)
{
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT };
    adc_oneshot_new_unit(&init_config, &adc1_handle);

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_9,       // ← КВАНТУВАННЯ: навмисно занижена розрядність (512 рівнів замість 4096)
        .atten    = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &chan_config);

    while (1) {
        int raw = 0;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL, &raw);
        ESP_LOGI(TAG, "raw(9-bit)=%d", raw);

        // ← ДИСКРЕТИЗАЦІЯ: спробуй три значення затримки по черзі
        vTaskDelay(pdMS_TO_TICKS(20));       // ① часта дискретизація (~50 Гц)
        // vTaskDelay(pdMS_TO_TICKS(200));    // ② помірна (~5 Гц)
        // vTaskDelay(pdMS_TO_TICKS(1000));   // ③ рідка (~1 Гц)
    }
}
