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
    // Апаратний LEDC генерує імпульси і під час task delay; це не програмний PWM.
    while (true) {
        // Рівні 0, 255, 511, 767, 1023 після цілочисельного ділення;
        // duty — кількість кроків HIGH, не відсотки.
        for (uint32_t level = 0; level <= 4; ++level) {
            const uint32_t duty = level * duty_max / 4;
            // set_duty(mode, channel, duty) готує нове значення; update_duty застосовує його.
            // Обидва повертають esp_err_t. На відміну від analogWrite, потрібні два явні кроки.
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
            // vTaskDelay приймає ticks; pdMS_TO_TICKS переводить ms за частотою RTOS.
            // На відміну від delay(ms), одиниця аргументу тут — tick; інші задачі можуть працювати.
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
