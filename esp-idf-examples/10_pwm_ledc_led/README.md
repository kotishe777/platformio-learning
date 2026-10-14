# 10 — LEDC: плавна яскравість LED

## Мета прикладу

Апаратний PWM без ручного перемикання GPIO.
Незалежний C++ ESP-IDF проєкт для ESP32-S3 DevKitC-1 / N16R8, API локального SDK **5.5.0**.
16 MB Flash; PSRAM вимкнена. Arduino Framework і PlatformIO не використовуються.

## Схема підключення

GPIO4 → 330 Ω → анод зовнішнього LED; катод → GND. LED активний HIGH.

Плату підключіть USB-кабелем з даними. Стандартна консоль — через USB-to-UART роз’єм.
Перевірте GPIO за схемою своєї плати; цифрові сигнали мають рівні 3.3 V, не подавайте 5 V на GPIO.

## Нові функції ESP-IDF та їхні параметри

| API | Призначення та параметри |
|---|---|
| `ledc_timer_config(&config)` | Частота, resolution, номер timer, clock і speed mode. |
| `ledc_channel_config(&config)` | GPIO, channel, timer, початковий duty та hpoint. |
| `ledc_set_duty(mode, channel, duty)` | Готує значення duty для каналу. |
| `ledc_update_duty(mode, channel)` | Застосовує duty; апаратний PWM працює незалежно від циклу. |

Докладні пояснення структур, полів, результатів та порядку викликів є безпосередньо у `main/main.cpp`.
`constexpr` задає константи компіляції; `extern "C"` зберігає точку входу;
`ESP_ERROR_CHECK` перевіряє код помилки, не виміряне значення.
`vTaskDelay` блокує поточну task, дозволяючи працювати іншим; його аргумент — RTOS ticks.

## Як працює програма — покроково

1. Налаштувати timer 5 kHz/10 bit.
2. Прив’язати channel 0 до GPIO4.
3. Оновлювати duty кроками кожні 100 ms.

Частота — періоди за секунду; resolution — кількість кроків duty; duty cycle — частка часу HIGH. 1023/1024 ≈ 99.90%, тобто максимальний duty тут близький до 100%, але не рівний йому.

## Відповідність Arduino ↔ ESP-IDF

| Arduino / звичний підхід | ESP-IDF |
|---|---|
| `analogWrite(pin, value)` | LEDC timer/channel + set_duty/update_duty |
| `ledcWrite() в Arduino-ESP32` | ledc_set_duty() + ledc_update_duty() |

Це відповідності за метою; аргументи та семантика можуть відрізнятися.

## Що студент повинен побачити або виміряти

LED циклічно змінює яскравість; аналізатор показує PWM з періодом 200 мкс і різним часом HIGH.
Це очікування для експерименту; роботу на реальній платі тут не перевірено.

## Експерименти для самостійної роботи

Змініть паузу з 100 на 500 ms, щоб краще бачити ступені яскравості.

## Запуск

З цього каталогу в терміналі з активованим ESP-IDF:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

`set-target` — при першій конфігурації; `COMx` замініть портом плати. Вихід з monitor — `Ctrl+]`.
Стан перевірки всіх проєктів — у [загальному README](../README.md).
Офіційний довідник: [ESP-IDF 5.5 ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/index.html).
