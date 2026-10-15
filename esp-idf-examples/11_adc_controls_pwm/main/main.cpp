/*
 * Lesson 2.6 — ESP-IDF example 11: potentiometer (ADC) controls LED brightness (PWM).
 * Board: ESP32-S3 DevKitC-1 (N16R8), ESP-IDF 5.5 via PlatformIO (espressif32@6.12.0).
 * Every 50 ms: read ADC1_CH6 (GPIO7, 12-bit, 0..4095),
 * scale to 10-bit duty (0..1023) and apply to LEDC_CHANNEL_0 on GPIO4 (5 kHz).
 * Changed from the lesson: ADC_CHANNEL_0 (GPIO1) -> ADC_CHANNEL_6 (GPIO7),
 * plus a log line every 10 loops (≈ 0.5 s).
 *
 * ============================================================================
 * СХЕМА ПІДКЛЮЧЕННЯ
 * ============================================================================
 *   1. ESP32 GPIO4               -> резистор 220–330 Ом -> LED анод (довга ніжка)
 *   2. LED катод (коротка)       -> GND
 *   Потенціометр B10K (валом до себе, ніжками вниз):
 *   3. Ліва ніжка                -> GND
 *   4. Середня ніжка (повзунок)  -> ESP32 GPIO7 (ADC1_CH6)
 *   5. Права ніжка               -> ESP32 3V3   (НЕ 5V!)
 *   6. ESP32 USB                 -> комп'ютер
 *
 *   GPIO4 ---[220 Ом]---|>|--- GND                 LED
 *   3V3 ---- права ---┐
 *                    [▓]<-- середня ---- GPIO7     потенціометр
 *   GND ---- ліва  ---┘
 *
 * ============================================================================
 * ЩО ПЕРЕВІРИТИ
 * ============================================================================
 *   - Обертання ручки -> плавна зміна яскравості LED.
 *   - Лог ≈ кожні 0.5 с: "raw=..., duty=..." (duty ≈ raw / 4).
 *   - Мінімум -> raw ≈ 0, duty ≈ 0, LED не світить.
 *   - Максимум -> raw = 4095, duty = 1023; останній шматочок оберту нічого
 *     не змінює (насичення АЦП ≈ 3.1 В).
 *   - Яскравість росте швидко на початку і майже не змінюється далі:
 *     око сприймає яскравість нелінійно — це нормально.
 *   - Експеримент: пауза 50 -> 500 мс -> реакція на ручку стає «ривками».
 *
 * Формула: duty = raw * 1023 / 4095 (переклад з «лінійки» АЦП на «лінійку» PWM).
 *
 * Arduino -> ESP-IDF:
 *   analogRead()  -> adc_oneshot_read()
 *   analogWrite() -> ledc_set_duty() + ledc_update_duty()
 * 
 * ~/.platformio/penv/bin/pio run -d esp-idf-examples/11_adc_controls_pwm -t upload -t monitor
 */

#include <cstdint>
#include <cinttypes>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/ledc.h"

// constexpr — константа часу компіляції; імена GPIO та enum беруться з ESP-IDF.
// uint32_t — 32-бітне беззнакове число.
constexpr gpio_num_t pwm_gpio = GPIO_NUM_4;
constexpr uint32_t adc_max = (1U << 12) - 1;
constexpr uint32_t duty_max = (1U << 10) - 1;
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
    // Крок 3: timer задає частоту PWM і роздільну здатність (кількість кроків duty).
    // На ESP32-S3 підтримується LEDC_LOW_SPEED_MODE; це не «повільне» виконання CPU.
    ledc_timer_config_t pwm_timer_config{};
    pwm_timer_config.speed_mode = LEDC_LOW_SPEED_MODE;
    pwm_timer_config.timer_num = LEDC_TIMER_0; // Номер апаратного таймера LEDC.
    pwm_timer_config.duty_resolution = LEDC_TIMER_10_BIT;
    pwm_timer_config.freq_hz = 5000; // Частота періодів, одиниця — Hz.
    pwm_timer_config.clk_cfg = LEDC_AUTO_CLK; // Драйвер вибирає придатне джерело clock.
    // ledc_timer_config(&config) застосовує параметри; повертає esp_err_t.
    ESP_ERROR_CHECK(ledc_timer_config(&pwm_timer_config));
    // Крок 4: channel з'єднує timer з GPIO та задає тривалість HIGH — duty.
    ledc_channel_config_t pwm_channel_config{};
    pwm_channel_config.gpio_num = pwm_gpio;
    pwm_channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
    pwm_channel_config.channel = LEDC_CHANNEL_0;
    pwm_channel_config.timer_sel = LEDC_TIMER_0;
    pwm_channel_config.intr_type = LEDC_INTR_DISABLE; // Callback/переривання тут не потрібні.
    pwm_channel_config.duty = 0; // Початково вихід LOW.
    pwm_channel_config.hpoint = 0; // HIGH починається на початку періоду.
    ESP_ERROR_CHECK(ledc_channel_config(&pwm_channel_config));
    // Апаратний LEDC генерує імпульси і під час task delay; це не програмний PWM.
    while (true) {
        // adc_oneshot_read(handle, channel, &raw) виконує одне перетворення ADC.
        // Повертає esp_err_t, а RAW пише через третій аргумент int*, на відміну від analogRead().
        int adc_raw = 0; // int потрібен саме для вихідного параметра драйвера.
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_6, &adc_raw));
        // static_cast явно переводить невід'ємний RAW у uint32_t до множення.
        // Найгірший добуток 4095 * 1023 = 4189185: у uint32_t він уміщується.
        const uint32_t duty = static_cast<uint32_t>(adc_raw) * duty_max / adc_max;
        // set_duty(mode, channel, duty) готує нове значення; update_duty застосовує його.
        // Обидва повертають esp_err_t. На відміну від analogWrite, потрібні два явні кроки.
        ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
        ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));

        // Друк раз на 10 проходів (≈ кожні 0.5 с), щоб не засмічувати монітор:
        // цикл крутиться кожні 50 мс, тобто 20 разів за секунду.
        static uint32_t loop_count = 0;
        if (++loop_count % 10 == 0) {
            ESP_LOGI("adc_pwm", "raw=%d, duty=%" PRIu32, adc_raw, duty);
        }

        // vTaskDelay приймає ticks; pdMS_TO_TICKS переводить ms за частотою RTOS.
        // На відміну від delay(ms), одиниця аргументу тут — tick; інші задачі можуть працювати.
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
