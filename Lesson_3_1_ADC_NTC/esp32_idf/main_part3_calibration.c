#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"           // новий інклюд: типи й функції калібрування
#include "esp_adc/adc_cali_scheme.h"    // новий інклюд: конкретна схема калібрування (curve fitting / line fitting)
#include "esp_log.h"

#define ADC_UNIT     ADC_UNIT_1
#define ADC_CHANNEL  ADC_CHANNEL_6

static const char *TAG = "ADC_CALI";

void app_main(void)
{
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT };
    adc_oneshot_new_unit(&init_config, &adc1_handle);

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,   // назад до повних 12 біт
        .atten    = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &chan_config);

    // --- НОВЕ: створюємо об'єкт калібрування ---
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id  = ADC_UNIT,
        .chan     = ADC_CHANNEL,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle); // читає заводські дані з eFuse чипа

    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Заводське калібрування недоступне на цьому чипі, використовуємо приблизну формулу");
    }

    while (1) {
        int raw = 0;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL, &raw);

        int voltage_mv_calibrated = 0;
        adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv_calibrated); // ТОЧНЕ переведення з урахуванням eFuse-даних чипа

        float voltage_mv_naive = (raw / 4095.0f) * 3300.0f;               // ПРИБЛИЗНЕ переведення "на око" (як у лекції)

        ESP_LOGI(TAG, "raw=%4d   calibrated=%d mV   naive=%.0f mV   diff=%d mV",
                 raw, voltage_mv_calibrated, voltage_mv_naive,
                 voltage_mv_calibrated - (int)voltage_mv_naive);

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}
