#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// constexpr задає незмінні значення, відомі під час компіляції.
// gpio_num_t — тип номера GPIO в ESP-IDF; GPIO_NUM_4 означає саме GPIO4.
// int64_t — знаковий 64-бітний тип часу в мікросекундах; 1000 мкс = 1 ms.
constexpr gpio_num_t led_gpio = GPIO_NUM_4;
constexpr gpio_num_t button_gpio = GPIO_NUM_5;
constexpr int64_t debounce_us = 30 * 1000;

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
    // intr_type вимикає переривання: рівень кнопки перевіряємо опитуванням у задачі main.
    button_config.intr_type = GPIO_INTR_DISABLE;
    // gpio_config(&button_config) спочатку застосовує режим input і pull-up;
    // лише після цього gpio_get_level має сенс. ESP_ERROR_CHECK перевіряє код результату.
    ESP_ERROR_CHECK(gpio_config(&button_config));

    // gpio_get_level(button_gpio) читає рівень саме входу кнопки й повертає 0 або 1,
    // а не код помилки. Це відповідник digitalRead(); input потрібно налаштувати раніше.
    // Pull-up тримає відпущену кнопку HIGH, натискання замикає GPIO на GND (LOW).
    // Порівняння з 0 перетворює активний LOW на bool «натиснуто».
    bool last_raw_pressed = gpio_get_level(button_gpio) == 0;
    // Raw — поточне читання, stable — підтверджений стан після вікна стабільності.
    // Беремо початковий рівень як stable: затиснута під час старту кнопка не є новим натисканням.
    bool stable_pressed = last_raw_pressed;
    // esp_timer_get_time() не має аргументів і повертає int64_t: кількість мікросекунд
    // від ініціалізації ESP Timer. 1000 мкс = 1 ms; це не millis() у мілісекундах.
    // За метою схоже на micros(), але тип результату інший. Читання часу не чекає,
    // не запускає callback і не потребує створення власного таймера.
    int64_t changed_at_us = esp_timer_get_time();
    bool led_on = false;

    while (true) {
        // Крок 1: зчитуємо raw; через опитування зміни між двома читаннями можемо пропустити.
        const bool raw_pressed = gpio_get_level(button_gpio) == 0;
        // Крок 2: читаємо час у мкс, щоб виміряти тривалість незмінного raw стану.
        const int64_t now_us = esp_timer_get_time();
        // Крок 3: кожна помічена зміна raw починає відлік 30 ms заново.
        if (raw_pressed != last_raw_pressed) {
            last_raw_pressed = raw_pressed;
            changed_at_us = now_us;
        }
        // Крок 4: приймаємо новий стан лише після 30 ms без помічених змін.
        // Різниця часових позначок має ті самі одиниці (мкс), що й debounce_us.
        if (raw_pressed != stable_pressed && now_us - changed_at_us >= debounce_us) {
            stable_pressed = raw_pressed;
            // Крок 5: перемикаємо LED тільки при підтвердженому натисканні (LOW).
            // Підтверджене відпускання лише дозволяє розпізнати наступне натискання;
            // утримання кнопки не створює повторних перемикань.
            if (stable_pressed) {
                led_on = !led_on;
                ESP_ERROR_CHECK(gpio_set_level(led_gpio, led_on));
            }
        }
        // vTaskDelay(1): аргумент 1 — один tick планувальника, НЕ одна мілісекунда.
        // При CONFIG_FREERTOS_HZ = 100 один tick = 10 ms; pdMS_TO_TICKS(1) дав би 0.
        // Виклик тимчасово блокує лише main task, дозволяючи іншим задачам працювати;
        // нічого не повертає. Час паузи залежить від фази tick і планування.
        // Коротка пауза запобігає постійному зайняттю CPU цим циклом.
        vTaskDelay(1);
    }
}
