#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// constexpr задає незмінні значення, відомі під час компіляції.
// gpio_num_t — тип номера GPIO в ESP-IDF; GPIO_NUM_4 означає саме GPIO4.
// uint32_t задає 32-бітний беззнаковий інтервал у мілісекундах.
constexpr gpio_num_t first_led_gpio = GPIO_NUM_4;
constexpr gpio_num_t second_led_gpio = GPIO_NUM_6;
constexpr uint32_t switch_interval_ms = 500;

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
    // pull_up_en вимикає внутрішнє підтягування до HIGH; для виходу LED воно не потрібне.
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    // pull_down_en вимикає внутрішнє підтягування до GND (LOW).
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    // intr_type вимикає GPIO-переривання: для цих виходів обробник переривання не потрібний.
    config.intr_type = GPIO_INTR_DISABLE;
    // gpio_config(&config) застосовує налаштування до пінів із маски;
    // & передає адресу структури, з якої драйвер читає поля.
    // Це аналог мети pinMode(), але тут одночасно задаємо режим, підтягування та переривання.
    // Результат — esp_err_t: ESP_OK при успіху, код помилки при хибних параметрах.
    // ESP_ERROR_CHECK виконує виклик і перевіряє результат: при помилці друкує
    // діагностику та викликає abort(), а не повертає bool і не повторює операцію.
    ESP_ERROR_CHECK(gpio_config(&config));

    bool first_led_on = true;
    while (true) {
        // gpio_set_level(gpio, level) записує рівень виходу: перший аргумент — GPIO,
        // другий — 0 для LOW, 1 для HIGH (bool теж перетворюється на 0/1).
        // Як digitalWrite(), керує рівнем; на відміну від нього повертає esp_err_t.
        // ESP_ERROR_CHECK перевіряє ESP_OK. GPIO треба спочатку налаштувати як output.
        // Для LED з катодом на GND HIGH означає світіння; MCU утримує рівень під час паузи.
        ESP_ERROR_CHECK(gpio_set_level(first_led_gpio, first_led_on));
        // ! інвертує bool, тому другий LED має протилежний рівень.
        // Це два послідовні записи GPIO, а не одночасне перемикання апаратури.
        ESP_ERROR_CHECK(gpio_set_level(second_led_gpio, !first_led_on));
        // vTaskDelay приймає кількість ticks планувальника, а не мілісекунди.
        // pdMS_TO_TICKS(ms) перетворює ms за CONFIG_FREERTOS_HZ: при 100 Hz tick = 10 ms.
        // Arduino delay(ms) приймає ms; тут аргумент має бути в ticks. Блокується
        // поточна FreeRTOS task, і CPU може виконувати інші задачі; результату немає.
        // Пауза залежить від фази tick та планування, тому це не точний апаратний таймінг.
        vTaskDelay(pdMS_TO_TICKS(switch_interval_ms));
        first_led_on = !first_led_on;
    }
}
