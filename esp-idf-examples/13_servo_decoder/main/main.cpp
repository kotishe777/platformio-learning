// KY-040 + servo: simple ESP-IDF 5.5 C++ example for ESP32-S3.
// Encoder: CLK=GPIO4, DT=GPIO5, SW=GPIO6, +=3V3, GND=GND.
// Servo: signal=GPIO7, separate 5V power supply, common GND.

#include <cstdint>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

constexpr gpio_num_t ENC_A = GPIO_NUM_4;
constexpr gpio_num_t ENC_B = GPIO_NUM_5;
constexpr gpio_num_t BUTTON = GPIO_NUM_6;
constexpr gpio_num_t SERVO = GPIO_NUM_7;

constexpr int COUNTS_PER_CLICK = 4;  // Measured on this KY-040.
constexpr int DEGREES_PER_CLICK = 5;
constexpr int CENTER_ANGLE = 90;

constexpr int PWM_FREQUENCY_HZ = 50;
constexpr int PWM_RESOLUTION_STEPS = 16384;  // 14 bits: 2^14.
constexpr int PWM_PERIOD_US = 20000;        // 50 Hz -> 20 ms.

// Convert the desired angle to a 1,000..2,000 us servo pulse.
uint32_t angle_to_duty(int angle)
{
    const int pulse_us = 1000 + angle * 1000 / 180;
    return pulse_us * PWM_RESOLUTION_STEPS / PWM_PERIOD_US;
}

void servo_init()
{
    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.timer_num = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_14_BIT;
    timer.freq_hz = PWM_FREQUENCY_HZ;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel{};
    channel.gpio_num = SERVO;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = angle_to_duty(CENTER_ANGLE);
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

void servo_set_angle(int angle)
{
    // Standard duty update: does NOT require fade service.
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, angle_to_duty(angle));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

pcnt_unit_handle_t encoder_init()
{
    pcnt_unit_config_t config{};
    config.low_limit = -30000;
    config.high_limit = 30000;

    pcnt_unit_handle_t unit = nullptr;
    ESP_ERROR_CHECK(pcnt_new_unit(&config, &unit));

    // Channel A: count edges on A and use B to determine direction.
    pcnt_chan_config_t config_a{};
    config_a.edge_gpio_num = ENC_A;
    config_a.level_gpio_num = ENC_B;
    pcnt_channel_handle_t a = nullptr;
    ESP_ERROR_CHECK(pcnt_new_channel(unit, &config_a, &a));
    pcnt_channel_set_edge_action(
        a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(
        a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    // Channel B: count edges on B and use A to determine direction (X4).
    pcnt_chan_config_t config_b{};
    config_b.edge_gpio_num = ENC_B;
    config_b.level_gpio_num = ENC_A;
    pcnt_channel_handle_t b = nullptr;
    ESP_ERROR_CHECK(pcnt_new_channel(unit, &config_b, &b));
    pcnt_channel_set_edge_action(
        b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(
        b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    ESP_ERROR_CHECK(pcnt_unit_enable(unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(unit));
    ESP_ERROR_CHECK(pcnt_unit_start(unit));
    return unit;
}

// ESP-IDF calls this entry point using C linkage.
extern "C" void app_main()
{
    gpio_set_direction(BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON, GPIO_PULLUP_ONLY);

    pcnt_unit_handle_t encoder = encoder_init();
    servo_init();

    int last_clicks = 0;
    bool was_pressed = false;
    TickType_t last_press_time = 0;

    ESP_LOGI("DEMO", "Ready: 4 counts/click, 5 degrees/click; button -> 90 deg");

    while (true) {
        // PCNT counts 4 quadrature transitions per physical click.
        int raw = 0;
        pcnt_unit_get_count(encoder, &raw);
        const int clicks = raw / COUNTS_PER_CLICK;

        if (clicks != last_clicks) {
            int angle = CENTER_ANGLE + clicks * DEGREES_PER_CLICK;
            if (angle < 0) angle = 0;
            if (angle > 180) angle = 180;

            servo_set_angle(angle);
            ESP_LOGI("DEMO", "raw=%d clicks=%d angle=%d", raw, clicks, angle);
            last_clicks = clicks;
        }

        // Button press resets the encoder count and centers the servo.
        const bool pressed = (gpio_get_level(BUTTON) == 0);
        const TickType_t now = xTaskGetTickCount();
        if (pressed && !was_pressed &&
            (now - last_press_time) >= pdMS_TO_TICKS(50)) {
            pcnt_unit_clear_count(encoder);
            last_clicks = 0;
            servo_set_angle(CENTER_ANGLE);
            ESP_LOGI("DEMO", "Button -> center (90 deg)");
            last_press_time = now;
        }
        was_pressed = pressed;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
