# 01 — Blink: GPIO output

## Мета прикладу

Мета: налаштувати GPIO як вихід і програмно змінювати його рівень.
Ціль: ESP32-S3 DevKitC-1 / N16R8; API ESP-IDF 5.5.x, C++, без Arduino і PlatformIO.
`sdkconfig.defaults`: 16 MB Flash, PSRAM вимкнена.

## Схема підключення

GPIO4 → резистор 330 Ω → анод зовнішнього LED; катод → GND.
LED активний HIGH. Живлення плати — USB; перевірте GPIO4 за схемою своєї плати.
На GPIO не подавайте 5 V; тут використовуються логічні рівні 3.3 V.
Вбудований LED не використовується.

## Нові функції ESP-IDF та їхні параметри

| Функція або макрос | Призначення, аргументи та результат |
|---|---|
| `gpio_config(&config)` | Застосовує структуру за переданою адресою до GPIO з `pin_bit_mask`; повертає `ESP_OK` або код помилки. |
| `gpio_set_level(gpio, level)` | GPIO виходу та рівень `0` / `1`; повертає `ESP_OK` або код помилки. |
| `vTaskDelay(ticks)` | Блокує поточну задачу на задану кількість RTOS ticks; нічого не повертає. |

`constexpr` задає константи часу компіляції; `extern "C"` зберігає ім'я точки входу для стартового коду ESP-IDF. `ESP_ERROR_CHECK(expr)` перевіряє код `esp_err_t`: при помилці друкує діагностику й викликає `abort()`. `pdMS_TO_TICKS(ms)` переводить ms у ticks відповідно до `CONFIG_FREERTOS_HZ`.

`gpio_config_t{}` починається з нульових полів. `pin_bit_mask` вибирає піни:
`1ULL << pin` ставить біт номера GPIO, `|` об'єднує кілька пінів.
`mode` задає input/output; `pull_up_en` і `pull_down_en` керують внутрішніми
підтягуваннями до HIGH/LOW; `intr_type` тут вимикає переривання.
Не присвоєні поля pull лишаються нульовими — підтягування вимкнене.

## Як працює програма — покроково

1. Структура вибирає GPIO4 та задає режим output без підтягувань і переривань.
2. `gpio_config` застосовує її до апаратного GPIO перед записом рівнів.
3. Цикл задає HIGH, чекає близько 500 ms, задає LOW і знову чекає 500 ms.
4. GPIO утримує записаний рівень навіть коли main task заблокована затримкою.

`pin_bit_mask` вибирає GPIO; `mode` вмикає вихідний драйвер,
`pull_up_en` / `pull_down_en` вимикають підтягування, `intr_type` вимикає IRQ.
`gpio_set_level` змінює вихідний рівень, який MCU утримує до наступного запису.
`ESP_ERROR_CHECK` зупиняє програму при помилці API.
`app_main()` вже працює в main task. `vTaskDelay` приймає ticks, а не мілісекунди:
`pdMS_TO_TICKS` перетворює час відповідно до `CONFIG_FREERTOS_HZ`.
Поки задача чекає, LED зберігає рівень; таймінг має похибку ticks і планувальника.

## Відповідність Arduino ↔ ESP-IDF

| Arduino | ESP-IDF |
|---|---|
| `pinMode(pin, OUTPUT)` | `gpio_config(&config)` з `GPIO_MODE_OUTPUT` |
| `digitalWrite(pin, level)` | `gpio_set_level(gpio, level)` |
| `delay(ms)` | `vTaskDelay(pdMS_TO_TICKS(ms))` |

Відповідності наведено за метою; параметри, типи результатів і семантика можуть відрізнятися.

## Запуск

В активованому терміналі ESP-IDF, з каталогу цього проєкту:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

`set-target` — при першій конфігурації; `COMx` замініть портом плати.
Використовуйте USB-to-UART для стандартної консолі; вихід з monitor: `Ctrl+]`.

## Що студент повинен побачити або виміряти

LED світиться приблизно 500 ms і не світиться 500 ms: повний цикл близько 1 s.
На логічному аналізаторі GPIO4 видно чергування HIGH і LOW.

## Експерименти для самостійної роботи

Змініть `half_period_ms` на `1000`. Повний цикл має зрости приблизно до 2 s.

Джерело: [GPIO ESP-IDF 5.5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/peripherals/gpio.html).
