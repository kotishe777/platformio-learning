#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"           // esp_timer_get_time() — для неблокуючого розкладу читань
#include "esp_log.h"

#define ADC_UNIT       ADC_UNIT_1
#define ADC_CHANNEL    ADC_CHANNEL_6
#define LED_PIN        GPIO_NUM_5

#define SAMPLES            16          // оверсемплінг: скільки замірів усереднюємо за один цикл
#define SAMPLE_PERIOD_US   (50 * 1000) // період дискретизації: 50 мс = 20 Гц
#define THRESHOLD_ON_RAW   1500        // стає темно нижче цього "сирого" рівня
#define THRESHOLD_OFF_RAW  1800        // вважаємо "знову світло" вище цього рівня

static const char *TAG = "ADC_FINAL";

// Один усереднений замір — оверсемплінг проти шуму (Частина 4 лекції)
static int read_averaged(adc_oneshot_unit_handle_t handle)
{
    long sum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw = 0;
        adc_oneshot_read(handle, ADC_CHANNEL, &raw);
        sum += raw;
    }
    return (int)(sum / SAMPLES);
}

void app_main(void)
{
    // --- налаштування виходу LED ---
    gpio_config_t led_conf = {
        .pin_bit_mask = 1ULL << LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_conf);

    // --- налаштування АЦП ---
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT };
    adc_oneshot_new_unit(&init_config, &adc1_handle);

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &chan_config);

    // --- калібрування (для друку вольтів у лог, самій логіці порогу не потрібне) ---
    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT, .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);

    bool led_on = false;               // офіційний, підтверджений гістерезисом стан
    int64_t last_sample = 0;           // мітка часу останнього виміру (мкс)

    while (1) {
        int64_t now = esp_timer_get_time();

        if (now - last_sample >= SAMPLE_PERIOD_US) {   // неблокуюча дискретизація за розкладом
            last_sample = now;

            int level = read_averaged(adc1_handle);    // квантоване, усереднене значення

            int mv = 0;
            adc_cali_raw_to_voltage(cali_handle, level, &mv);

            // --- гістерезис: два різні пороги замість одного (аналоговий аналог дебаунсу) ---
            if (!led_on && level < THRESHOLD_ON_RAW) {
                led_on = true;                          // стало темно → вмикаємо
            } else if (led_on && level > THRESHOLD_OFF_RAW) {
                led_on = false;                         // стало досить світло → вимикаємо
            }
            gpio_set_level(LED_PIN, led_on);

            ESP_LOGI(TAG, "level=%4d (%d mV)   led=%s", level, mv, led_on ? "ON" : "off");
        }

        vTaskDelay(pdMS_TO_TICKS(5)); // невелика пауза — лишає час планувальнику FreeRTOS, не впливає на точність дискретизації
    }
}
