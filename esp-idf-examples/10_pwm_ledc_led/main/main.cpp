/*
 * Lesson 2.6 — ESP-IDF example 10: LED brightness steps with LEDC PWM.
 * Board: ESP32-S3 DevKitC-1 (N16R8), ESP-IDF 5.5 via PlatformIO (espressif32@6.12.0).
 * LEDC_TIMER_0: 5 kHz, 10-bit (0..1023) -> LEDC_CHANNEL_0 -> GPIO4.
 * Duty steps 0 / 255 / 511 / 767 / 1023 every 100 ms (cycle ≈ 0.5 s).
 *
 * ============================================================================
 * СХЕМА ПІДКЛЮЧЕННЯ
 * ============================================================================
 *   1. ESP32 GPIO4               -> резистор 220–330 Ом -> LED анод (довга ніжка)
 *   2. LED катод (коротка)       -> GND
 *   3. ESP32 USB                 -> комп'ютер
 *
 *   GPIO4 ---[220 Ом]---|>|--- GND
 *
 * ============================================================================
 * ЩО ПЕРЕВІРИТИ
 * ============================================================================
 *   - LED сходинками: 0% -> 25% -> 50% -> 75% -> ~100% -> знову 0%.
 *   - Мерехтіння не видно (5000 періодів за секунду — швидше за око).
 *   - Експеримент: vTaskDelay(pdMS_TO_TICKS(100)) -> 500 мс: сходинки видно краще.
 *   - (Аналізатор) період 200 мкс; HIGH ≈ 0 / 50 / 100 / 150 / 200 мкс.
 *
 * Той самий LEDC, що в Servo 03 (урок 2.5), але інший сенс ширини імпульсу:
 *   серво — ширина = КУТ («лист з адресою», 50 Гц);
 *   LED   — частка часу HIGH = ЯСКРАВІСТЬ («кран зі струмом», 5 кГц).
 *
 * Arduino -> ESP-IDF:
 *   analogWrite() / ledcWrite() -> ledc_set_duty() + ledc_update_duty()
 * 
 * ~/.platformio/penv/bin/pio run -d esp-idf-examples/10_pwm_ledc_led -t upload -t monitor
 */

#include <cstdint>
#include <cinttypes>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"

// constexpr — константа часу компіляції; імена GPIO та enum беруться з ESP-IDF.
// uint32_t — 32-бітне беззнакове число.
constexpr gpio_num_t pwm_gpio = GPIO_NUM_4;
constexpr uint32_t duty_max = (1U << 10) - 1; // 10 bit → безпечний діапазон 0..1023.
// extern "C" зберігає ім'я app_main для стартового C-коду ESP-IDF.
// app_main уже виконується у FreeRTOS main task; setup()/loop() тут немає.
// ESP_ERROR_CHECK(expr) перевіряє esp_err_t: ESP_OK — успіх, інакше діагностика й abort().
extern "C" void app_main()
{
    // Крок 1: timer задає частоту PWM і роздільну здатність (кількість кроків duty).
    // На ESP32-S3 підтримується LEDC_LOW_SPEED_MODE; це не «повільне» виконання CPU.
    ledc_timer_config_t pwm_timer_config{};
    pwm_timer_config.speed_mode = LEDC_LOW_SPEED_MODE;
    pwm_timer_config.timer_num = LEDC_TIMER_0; // Номер апаратного таймера LEDC.
    pwm_timer_config.duty_resolution = LEDC_TIMER_10_BIT;
    pwm_timer_config.freq_hz = 5000; // Частота періодів, одиниця — Hz.
    pwm_timer_config.clk_cfg = LEDC_AUTO_CLK; // Драйвер вибирає придатне джерело clock.
    // ledc_timer_config(&config) застосовує параметри; повертає esp_err_t.
    ESP_ERROR_CHECK(ledc_timer_config(&pwm_timer_config));
    // Крок 2: channel з'єднує timer з GPIO та задає тривалість HIGH — duty.
    ledc_channel_config_t pwm_channel_config{};
    pwm_channel_config.gpio_num = pwm_gpio;
    pwm_channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
    pwm_channel_config.channel = LEDC_CHANNEL_0;
    pwm_channel_config.timer_sel = LEDC_TIMER_0;
    pwm_channel_config.intr_type = LEDC_INTR_DISABLE; // Callback/переривання тут не потрібні.
    pwm_channel_config.duty = 0; // Початково вихід LOW.
    pwm_channel_config.hpoint = 0; // HIGH починається на початку періоду.
    ESP_ERROR_CHECK(ledc_channel_config(&pwm_channel_config));
    // ТИМЧАСОВИЙ ЕКСПЕРИМЕНТ: рівні, підібрані під нелінійний зір,
    // пауза 1 с і друк поточного рівня в монітор.
    // Оригінал: 5 рівних кроків (0/255/511/767/1023) по 100 мс.
    constexpr uint32_t levels[] = {0, 8, 32, 128, 512, 1023};
    while (true) {
        for (uint32_t duty : levels) {
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
            // PRIu32 — правильний формат для uint32_t у printf/ESP_LOG; %% друкує знак %.
            ESP_LOGI("pwm", "duty=%" PRIu32 " (~%" PRIu32 "%%)", duty, duty * 100 / duty_max);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
