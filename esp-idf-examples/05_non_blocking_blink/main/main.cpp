/*
 * Lesson 2.6 — ESP-IDF example 05: two LEDs blink with independent intervals.
 * Board: ESP32-S3 DevKitC-1 (N16R8), ESP-IDF 5.5 via PlatformIO (espressif32@6.12.0).
 * LED1 (GPIO4) toggles every 200 ms, LED2 (GPIO6) every 700 ms.
 * No waiting for intervals: the loop reads esp_timer_get_time() and checks
 * elapsed time for each LED separately (like millis() in Arduino).
 *
 * ============================================================================
 * СХЕМА ПІДКЛЮЧЕННЯ (та сама, що в 02)
 * ============================================================================
 *   1. ESP32 GPIO4               -> резистор 220–330 Ом -> LED1 анод (довга ніжка)
 *   2. LED1 катод (коротка)      -> GND
 *   3. ESP32 GPIO6               -> ОКРЕМИЙ резистор 220–330 Ом -> LED2 анод
 *   4. LED2 катод (коротка)      -> GND
 *   5. ESP32 USB                 -> комп'ютер
 *
 *   GPIO4 ---[220 Ом]---|>|--- GND     LED1 (швидкий, 200 мс)
 *   GPIO6 ---[220 Ом]---|>|--- GND     LED2 (повільний, 700 мс)
 *
 * ============================================================================
 * ЩО ПЕРЕВІРИТИ
 * ============================================================================
 *   - LED1: ≈ 2.5 блимання/с (цикл 400 мс); LED2: ≈ 0.7 блимання/с (цикл 1400 мс).
 *   - За 10 с: LED1 засвітиться ≈ 25 разів, LED2 ≈ 7 разів.
 *   - Експеримент: second_interval_us = 1000 * 1000 -> LED2 цикл ≈ 2 с,
 *     LED1 свій ритм НЕ змінює (ритми незалежні).
 *
 * Ідея: «кухар з двома таймерами» — не чекати, а поглядати на годинник.
 * Два окремі if (не else if): обидва LED можуть перемкнутися за один прохід.
 *
 * Arduino -> ESP-IDF:
 *   millis() / micros() -> esp_timer_get_time()   (int64_t, мікросекунди)
 * 
 * ~/.platformio/penv/bin/pio run -d esp-idf-examples/05_non_blocking_blink -t upload -t monitor
 */

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// constexpr задає незмінні значення, відомі під час компіляції.
// gpio_num_t — тип номера GPIO в ESP-IDF; GPIO_NUM_4 означає саме GPIO4.
// int64_t — знаковий 64-бітний тип часу в мікросекундах; 1000 мкс = 1 ms.
constexpr gpio_num_t first_led_gpio = GPIO_NUM_4;
constexpr gpio_num_t second_led_gpio = GPIO_NUM_6;
constexpr int64_t first_interval_us = 200 * 1000;
constexpr int64_t second_interval_us = 700 * 1000;

// extern "C" зберігає ім'я app_main без C++ перетворення імен (name mangling):
// стартовий код ESP-IDF зможе знайти цю точку входу. Аргументів і результату немає.
// На відміну від setup()/loop(), app_main викликається один раз у задачі main FreeRTOS;
// повторення ми задаємо власним циклом. Створювати ще одну задачу тут не потрібно.
extern "C" void app_main()
{
    // gpio_config_t збирає налаштування GPIO для одного виклику драйвера.
    // {} обнуляє поля: це явна початкова конфігурація, а не читання стану апаратури.
    // Нульові pull_up_en/pull_down_en вимикають підтягування; далі змінюємо потрібні поля.
    gpio_config_t config{};
    // pin_bit_mask вибирає GPIO за номерами бітів, а не задає рівні виходів.
    // 1ULL — 64-бітна беззнакова одиниця; << ставить біт номера GPIO.
    // Побітове OR | об'єднує біти 4 і 6: 0x10 | 0x40 = 0x50.
    // Один gpio_config застосує однакові налаштування до обох пінів.
    config.pin_bit_mask = (1ULL << first_led_gpio) | (1ULL << second_led_gpio);
    // mode = GPIO_MODE_OUTPUT вмикає вихідний драйвер GPIO для керування LED.
    config.mode = GPIO_MODE_OUTPUT;
    // intr_type вимикає GPIO-переривання: для цих виходів обробник переривання не потрібний.
    config.intr_type = GPIO_INTR_DISABLE;
    // gpio_config(&config) застосовує налаштування до пінів із маски;
    // & передає адресу структури, з якої драйвер читає поля.
    // Це аналог мети pinMode(), але тут одночасно задаємо режим, підтягування та переривання.
    // Результат — esp_err_t: ESP_OK при успіху, код помилки при хибних параметрах.
    // ESP_ERROR_CHECK виконує виклик і перевіряє результат: при помилці друкує
    // діагностику та викликає abort(), а не повертає bool і не повторює операцію.
    ESP_ERROR_CHECK(gpio_config(&config));
    // gpio_set_level(gpio, level) записує рівень виходу: перший аргумент — GPIO,
    // другий — 0 для LOW, 1 для HIGH (bool теж перетворюється на 0/1).
    // Як digitalWrite(), керує рівнем; на відміну від нього повертає esp_err_t.
    // ESP_ERROR_CHECK перевіряє ESP_OK. GPIO треба спочатку налаштувати як output.
    // Для LED з катодом на GND HIGH означає світіння; MCU утримує рівень під час паузи.
    ESP_ERROR_CHECK(gpio_set_level(first_led_gpio, 0));
    ESP_ERROR_CHECK(gpio_set_level(second_led_gpio, 0));

    bool first_led_on = false;
    bool second_led_on = false;
    // esp_timer_get_time() не має аргументів і повертає int64_t: кількість мікросекунд
    // від ініціалізації ESP Timer. 1000 мкс = 1 ms; це не millis() у мілісекундах.
    // За метою схоже на micros(), але тип результату інший. Читання часу не чекає,
    // не запускає callback і не потребує створення власного таймера.
    int64_t first_changed_at_us = esp_timer_get_time();
    int64_t second_changed_at_us = first_changed_at_us;

    while (true) {
        // Крок 1: один знімок часу у мкс використовуємо для обох незалежних перевірок.
        const int64_t now_us = esp_timer_get_time();
        // Крок 2: перевіряємо, чи минуло 200 ms для першого LED, без очікування в цьому if.
        // Запис now_us починає наступний інтервал від обробленої події;
        // після довгої паузи пропущені перемикання не надолужуються.
        if (now_us - first_changed_at_us >= first_interval_us) {
            first_changed_at_us = now_us;
            first_led_on = !first_led_on;
            ESP_ERROR_CHECK(gpio_set_level(first_led_gpio, first_led_on));
        }
        // Крок 3: окремо перевіряємо 700 ms для другого LED; це if, а не else if,
        // тому за один прохід можуть перемкнутися обидва LED. Додаткові задачі не створюємо.
        if (now_us - second_changed_at_us >= second_interval_us) {
            second_changed_at_us = now_us;
            second_led_on = !second_led_on;
            ESP_ERROR_CHECK(gpio_set_level(second_led_gpio, second_led_on));
        }
        // vTaskDelay(1): аргумент 1 — один tick планувальника, НЕ одна мілісекунда.
        // При CONFIG_FREERTOS_HZ = 100 один tick = 10 ms; pdMS_TO_TICKS(1) дав би 0.
        // Виклик тимчасово блокує лише main task, дозволяючи іншим задачам працювати;
        // нічого не повертає. Час паузи залежить від фази tick і планування.
        // Коротка пауза запобігає постійному зайняттю CPU цим циклом.
        vTaskDelay(1);
    }
}
