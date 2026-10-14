#include <cstdint>
#include <cinttypes>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <atomic>

// constexpr — константа часу компіляції; імена GPIO та enum беруться з ESP-IDF.
constexpr gpio_num_t led_gpio = GPIO_NUM_4;
constexpr gpio_num_t button_gpio = GPIO_NUM_5;
// static обмежує видимість прапорця цим файлом; ресурс живе весь час роботи прошивки.
// atomic синхронізує ISR і main на різних ядрах; volatile цього не забезпечує.
static std::atomic<bool> button_event{false};
// static_assert перевіряє під час компіляції відсутність блокувань усередині atomic.
static_assert(std::atomic<bool>::is_always_lock_free, "ISR requires lock-free atomic");

// ISR (обробник переривання): void* — аргумент, переданий під час реєстрації; не потрібний.
// Тут немає логу, затримки або GPIO-драйвера: лише безпечне позначення події.
static void button_isr(void*)
{
    button_event.store(true, std::memory_order_relaxed);
}

// extern "C" зберігає ім'я app_main для стартового C-коду ESP-IDF.
// app_main уже виконується у FreeRTOS main task; setup()/loop() тут немає.
// ESP_ERROR_CHECK(expr) перевіряє esp_err_t: ESP_OK — успіх, інакше діагностика й abort().
extern "C" void app_main()
{
    // Крок 1: gpio_config_t — налаштування групи GPIO; {} обнуляє поля.
    gpio_config_t led_config{};
    // 1ULL — 64-бітна одиниця; << ставить біт GPIO4, маска вибирає саме цей пін.
    led_config.pin_bit_mask = 1ULL << led_gpio;
    led_config.mode = GPIO_MODE_OUTPUT;           // Вмикаємо вихідний драйвер.
    led_config.pull_up_en = GPIO_PULLUP_DISABLE;  // Без підтягування до HIGH.
    led_config.pull_down_en = GPIO_PULLDOWN_DISABLE; // Без підтягування до GND.
    led_config.intr_type = GPIO_INTR_DISABLE;     // LED не потребує переривань.
    // gpio_config(&структура): адресу передаємо через &; повертає esp_err_t.
    // Це ширший аналог pinMode: режим, підтягування та IRQ задаються разом.
    ESP_ERROR_CHECK(gpio_config(&led_config));
    // gpio_set_level(GPIO, рівень): 0 = LOW, 1 = HIGH; повертає код помилки.
    // Як digitalWrite, змінює вихід, але GPIO спочатку має бути output.
    ESP_ERROR_CHECK(gpio_set_level(led_gpio, 0));
    // Кнопка потребує окремого input; нульові поля {} означають початково вимкнені pull.
    gpio_config_t button_config{};
    button_config.pin_bit_mask = 1ULL << button_gpio; // Біт 5 вибирає GPIO5.
    button_config.mode = GPIO_MODE_INPUT;          // Читання рівня, а не керування виходом.
    button_config.pull_up_en = GPIO_PULLUP_ENABLE; // Відпущена кнопка тримається HIGH.
    button_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    // GPIO_INTR_NEGEDGE — спадний фронт HIGH→LOW, приблизний аналог FALLING в Arduino.
    button_config.intr_type = GPIO_INTR_NEGEDGE;
    ESP_ERROR_CHECK(gpio_config(&button_config));
    // install_isr_service(flags) створює сервіс GPIO ISR; 0 — без вимоги IRAM.
    // isr_handler_add(GPIO, callback, argument) прив'язує ISR до GPIO5; nullptr — без даних.
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(button_gpio, button_isr, nullptr));
    bool led_on = false;
    while (true) {
        // exchange(false) атомарно читає й скидає прапорець, не втрачаючи його між двома операціями.
        // relaxed достатньо: передаємо тільки подію, без інших спільних даних.
        if (button_event.exchange(false, std::memory_order_relaxed)) {
            led_on = !led_on;
            ESP_ERROR_CHECK(gpio_set_level(led_gpio, led_on));
        }
        // vTaskDelay(1) блокує main task на один RTOS tick, не на 1 ms.
        // При 100 Hz tick = 10 ms; pdMS_TO_TICKS(1) дав би 0. Інші задачі отримують CPU.
        vTaskDelay(1);
    }
}
