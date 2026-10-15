/*
 * Lesson 2.6 — ESP-IDF example 03: button polling (GPIO5) controls LED (GPIO4).
 * Board: ESP32-S3 DevKitC-1 (N16R8), ESP-IDF 5.5 via PlatformIO (espressif32@6.12.0).
 * GPIO5 is an input with the internal pull-up; button to GND is active LOW.
 * Every 20 ms the button is read: pressed -> LED ON, released -> LED OFF.
 *
 * ============================================================================
 * СХЕМА ПІДКЛЮЧЕННЯ
 * ============================================================================
 *   1. ESP32 GPIO4               -> резистор 220–330 Ом -> LED анод (довга ніжка)
 *   2. LED катод (коротка)       -> GND
 *   3. ESP32 GPIO5               -> кнопка (контакт A)
 *   4. Кнопка (контакт B)        -> GND
 *   5. ESP32 USB                 -> комп'ютер
 *
 *   GPIO4 ---[220 Ом]---|>|--- GND     LED
 *   GPIO5 ---[кнопка]-------- GND      (pull-up — всередині ESP32, резистор не потрібен)
 *
 *   4-ніжкова кнопка: брати контакти ПО ДІАГОНАЛІ — вони з'єднуються лише при натисканні.
 *
 * ============================================================================
 * ЩО ПЕРЕВІРИТИ
 * ============================================================================
 *   - Кнопка відпущена -> LED не світить (pull-up тримає HIGH = 1).
 *   - Кнопку тримаємо  -> LED світить     (кнопка замикає на GND = 0).
 *   - Експеримент: poll_interval_ms = 200 -> помітна затримка, швидкі клацання губляться.
 *
 * Arduino -> ESP-IDF:
 *   pinMode(pin, INPUT_PULLUP) -> gpio_config() з GPIO_MODE_INPUT + GPIO_PULLUP_ENABLE
 *   digitalRead(pin)           -> gpio_get_level(gpio)
 * 
 * ~/.platformio/penv/bin/pio run -d esp-idf-examples/03_button_polling -t upload -t monitor
 */

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// constexpr задає незмінні значення, відомі під час компіляції.
// gpio_num_t — тип номера GPIO в ESP-IDF; GPIO_NUM_4 означає саме GPIO4.
// uint32_t задає 32-бітний беззнаковий інтервал у мілісекундах.
constexpr gpio_num_t led_gpio = GPIO_NUM_4;
constexpr gpio_num_t button_gpio = GPIO_NUM_5;
constexpr uint32_t poll_interval_ms = 20;

// extern "C" зберігає ім'я app_main без C++ перетворення імен (name mangling):
// стартовий код ESP-IDF зможе знайти цю точку входу. Аргументів і результату немає.
// На відміну від setup()/loop(), app_main викликається один раз у задачі main FreeRTOS;
// повторення ми задаємо власним циклом. Створювати ще одну задачу тут не потрібно.
extern "C" void app_main()
{
    // gpio_config_t збирає налаштування GPIO для одного виклику драйвера.
    // {} обнуляє поля: це явна початкова конфігурація, а не читання стану апаратури.
    // Нульові pull_up_en/pull_down_en вимикають підтягування; далі змінюємо потрібні поля.
    gpio_config_t led_config{};
    // pin_bit_mask — набір вибраних GPIO: номер біта дорівнює номеру GPIO.
    // 1ULL — беззнакова одиниця типу unsigned long long, тут 64 біти;
    // << зсуває її до біта потрібного GPIO. Для GPIO4 це 1ULL << 4 = 0x10.
    // Широка маска потрібна, бо номери GPIO можуть бути більшими за 31.
    led_config.pin_bit_mask = 1ULL << led_gpio;
    // mode = GPIO_MODE_OUTPUT вмикає вихідний драйвер GPIO для керування LED.
    led_config.mode = GPIO_MODE_OUTPUT;
    // intr_type вимикає GPIO-переривання: для цих виходів обробник переривання не потрібний.
    led_config.intr_type = GPIO_INTR_DISABLE;
    // gpio_config(&led_config) застосовує налаштування до пінів із маски;
    // & передає адресу структури, з якої драйвер читає поля.
    // Це аналог мети pinMode(), але тут одночасно задаємо режим, підтягування та переривання.
    // Результат — esp_err_t: ESP_OK при успіху, код помилки при хибних параметрах.
    // ESP_ERROR_CHECK виконує виклик і перевіряє результат: при помилці друкує
    // діагностику та викликає abort(), а не повертає bool і не повторює операцію.
    ESP_ERROR_CHECK(gpio_config(&led_config));
    // gpio_set_level(gpio, level) записує рівень виходу: перший аргумент — GPIO,
    // другий — 0 для LOW, 1 для HIGH (bool теж перетворюється на 0/1).
    // Як digitalWrite(), керує рівнем; на відміну від нього повертає esp_err_t.
    // ESP_ERROR_CHECK перевіряє ESP_OK. GPIO треба спочатку налаштувати як output.
    // Для LED з катодом на GND HIGH означає світіння; MCU утримує рівень під час паузи.
    ESP_ERROR_CHECK(gpio_set_level(led_gpio, 0));

    // Окрема структура потрібна, бо кнопка — input, а LED — output.
    // {} також вимикає обидва pull за початковим нульовим значенням;
    // для кнопки нижче вмикаємо тільки pull-up, pull-down залишається вимкненим.
    gpio_config_t button_config{};
    // Біт 5 маски вибирає GPIO5 для кнопки; ця маска не змінює налаштування LED.
    button_config.pin_bit_mask = 1ULL << button_gpio;
    // GPIO_MODE_INPUT вмикає шлях читання рівня GPIO; вихідний драйвер не потрібний.
    button_config.mode = GPIO_MODE_INPUT;
    // pull_up_en вмикає внутрішній pull-up: без натискання вхід не залишається «плаваючим».
    button_config.pull_up_en = GPIO_PULLUP_ENABLE;
    // Pull-down до GND вимкнений, щоб він не протидіяв pull-up.
    button_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    // intr_type вимикає переривання: рівень кнопки перевіряємо опитуванням у задачі main.
    button_config.intr_type = GPIO_INTR_DISABLE;
    // gpio_config(&button_config) спочатку застосовує режим input і pull-up;
    // лише після цього gpio_get_level має сенс. ESP_ERROR_CHECK перевіряє код результату.
    ESP_ERROR_CHECK(gpio_config(&button_config));

    while (true) {
        // gpio_get_level(button_gpio) читає рівень саме входу кнопки й повертає 0 або 1,
        // а не код помилки. Це відповідник digitalRead(); input потрібно налаштувати раніше.
        // Pull-up тримає відпущену кнопку HIGH, натискання замикає GPIO на GND (LOW).
        // Порівняння з 0 перетворює активний LOW на bool «натиснуто».
        const bool pressed = gpio_get_level(button_gpio) == 0;
        // Передаємо pressed як рівень LED: true → 1, false → 0.
        // Цей приклад показує raw рівень без усунення дребезгу механічних контактів.
        ESP_ERROR_CHECK(gpio_set_level(led_gpio, pressed));
        // vTaskDelay приймає кількість ticks планувальника, а не мілісекунди.
        // pdMS_TO_TICKS(ms) перетворює ms за CONFIG_FREERTOS_HZ: при 100 Hz tick = 10 ms.
        // Arduino delay(ms) приймає ms; тут аргумент має бути в ticks. Блокується
        // поточна FreeRTOS task, і CPU може виконувати інші задачі; результату немає.
        // Пауза залежить від фази tick та планування, тому це не точний апаратний таймінг.
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
    }
}
