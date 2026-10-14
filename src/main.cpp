/*
 * Lesson 2.5 — Servo 03: explicit ESP-IDF LEDC timer and channel.
 * Board: ESP32-S3 DevKitC-1 (N16R8), Arduino Core 3.x (pioarduino).
 * LEDC_TIMER_0 (XTAL, 50 Hz, 14-bit) -> LEDC_CHANNEL_0 -> GPIO4.
 * Fix: ledc_set_duty() + ledc_update_duty() instead of ledc_set_duty_and_update().
 *
 * ============================================================================
 * СХЕМА ПІДКЛЮЧЕННЯ
 * ============================================================================
 *
 * Дві зони живлення, СПІЛЬНА ЛИШЕ ЗЕМЛЯ:
 *   - зона 3.3 В: ESP32 живиться від USB комп'ютера;
 *   - зона 5 В:   серво живиться від модуля живлення макетки (перемичка на 5V).
 *
 * Дроти серво (SG90 / MG90S):
 *   коричневий / чорний           = GND
 *   червоний (СЕРЕДНІЙ)           = «+» 5 В
 *   помаранчевий / жовтий / білий = Signal
 *   Підказка: найтемніший = GND, середній = «+», найсвітліший = Signal.
 *
 *   1. Модуль живлення +5V   -> шина «+» макетки
 *   2. Модуль живлення GND   -> шина «−» макетки
 *   3. Серво «+» (середній)  -> шина «+» (5 В)
 *   4. Серво GND             -> шина «−»
 *   5. Серво Signal          -> ESP32 GPIO4
 *   6. ESP32 GND             -> шина «−»     (СПІЛЬНА ЗЕМЛЯ — обов'язково!)
 *   7. ESP32 USB             -> комп'ютер
 *
 *   Модуль +5V ---> шина «+» ---> Серво «+»
 *   Модуль GND ---> шина «−» ---> Серво GND
 *                      ^
 *   ESP32 GND ---------+            (спільна земля)
 *   ESP32 GPIO4 ----------------> Серво Signal
 *   ESP32 USB ------------------> Комп'ютер
 *
 * ЗАБОРОНЕНО:
 *   - Серво «+» -> 3V3 або 5V-пін ESP32 (просідання напруги -> перезапуск плати).
 *   - З'єднувати +5V модуля з +5V плати (два джерела «сваряться»).
 *   - Перемикати дроти під напругою.
 *
 * Порядок увімкнення: перевірити дроти -> USB ESP32 -> модуль живлення ON.
 */

// ============================================================================
// 1. ПІДКЛЮЧЕННЯ БІБЛІОТЕК
// ============================================================================

// Arduino.h — Serial, delay() та інше.
#include <Arduino.h>

// cstdint — типи з точно відомим розміром (uint8_t, uint16_t, uint32_t).
#include <cstdint>

// driver/ledc.h — «низькорівневий» драйвер LEDC з ESP-IDF.
// Arduino Core 3.x побудований поверх ESP-IDF 5.x, тому ці функції доступні
// прямо з Arduino-проєкту. Саме ними всередині користується і ledcAttach().
#include "driver/ledc.h"

// ============================================================================
// 2. НАЛАШТУВАННЯ (КОНСТАНТИ)
// ============================================================================

// Пін сигналу серво. Тип int, бо структура ESP-IDF очікує саме int.
constexpr int SERVO_PIN = 4;

// 50 імпульсів за секунду, період 20 000 мкс.
constexpr uint32_t PWM_FREQ_HZ   = 50;
constexpr uint32_t PWM_PERIOD_US = 1000000UL / PWM_FREQ_HZ;

// 14 біт = 2^14 = 16 384 поділки на період (одна ≈ 1.22 мкс).
constexpr uint32_t PWM_STEPS = 1UL << 14;

// 1000 мкс = 0°, 2000 мкс = 180°.
constexpr uint16_t SERVO_MIN_US = 1000;
constexpr uint16_t SERVO_MAX_US = 2000;

// --- Явний вибір «деталей» LEDC ---------------------------------------------

// Метроном: таймер №0 блока LEDC.
// ВАЖЛИВО: це НЕ апаратний таймер з timerBegin() і НЕ Timer Group 0.
// Це окремий маленький таймер усередині периферії PWM.
constexpr ledc_timer_t SERVO_TIMER = LEDC_TIMER_0;

// Музикант: канал №0. Саме він «тримає» HIGH потрібну кількість поділок
// і виводить результат на пін.
constexpr ledc_channel_t SERVO_CHANNEL = LEDC_CHANNEL_0;

// Режим швидкості. У звичайного ESP32 було два режими (high і low speed),
// а в ESP32-S3 є ЛИШЕ low speed. «Low» тут не означає «повільний сигнал» —
// це лише назва групи таймерів.
constexpr ledc_mode_t SERVO_MODE = LEDC_LOW_SPEED_MODE;

// ============================================================================
// 3. КУТ -> ПОДІЛКИ (duty)
// ============================================================================
// Та сама математика, що й у Servo 01, зібрана в одну функцію:
//   кут -> мікросекунди -> поділки з округленням.
uint32_t angleToDuty(uint8_t angle) {
    if (angle > 180) angle = 180;  // Запобіжник
    const uint32_t pulseUs = SERVO_MIN_US
        + (static_cast<uint32_t>(angle) * (SERVO_MAX_US - SERVO_MIN_US)) / 180;
    return (pulseUs * PWM_STEPS + PWM_PERIOD_US / 2) / PWM_PERIOD_US;
}

// ============================================================================
// 4. «СТАНЬ НА КУТ angle» (ВИПРАВЛЕНА ВЕРСІЯ)
// ============================================================================
void setServoAngle(uint8_t angle) {
    const uint32_t duty = angleToDuty(angle);

    // Крок 1: записати нове значення duty у «чернетку» каналу.
    // Сигнал на піні від цього ще НЕ змінюється.
    esp_err_t err = ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, duty);

    // Крок 2: «застосувати чернетку». Канал почне використовувати нове
    // значення з наступного періоду (до 20 мс), тож імпульс не обірветься посередині.
    // Виконуємо, лише якщо крок 1 пройшов успішно.
    if (err == ESP_OK) {
        err = ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
    }

    // esp_err_t — код результату ESP-IDF: ESP_OK = 0 (усе добре), інше = помилка.
    // esp_err_to_name() перетворює код на зрозумілий текст, наприклад "ESP_ERR_INVALID_ARG".
    if (err != ESP_OK) {
        Serial.printf("LEDC duty error: %s\n", esp_err_to_name(err));
        return;
    }

    Serial.printf("angle=%u deg, duty=%lu\n",
                  static_cast<unsigned>(angle),
                  static_cast<unsigned long>(duty));
}

// ============================================================================
// 5. setup() — налаштовуємо LEDC «вручну», у два кроки
// ============================================================================
void setup() {
    Serial.begin(115200);

    // --- Крок 1. Налаштувати МЕТРОНОМ (таймер) --------------------------------
    // «= {}» заповнює всі поля структури нулями. Це захищає від «сміття»
    // в полях, які ми явно не задаємо.
    ledc_timer_config_t timer = {};
    timer.speed_mode      = SERVO_MODE;          // Група таймерів (у S3 лише low speed)
    timer.timer_num       = SERVO_TIMER;         // Який саме таймер: №0
    timer.duty_resolution = LEDC_TIMER_14_BIT;   // Лінійка на 16 384 поділки
    timer.freq_hz         = PWM_FREQ_HZ;         // 50 Гц
    // Джерело тактування — кварц XTAL 40 МГц: дуже стабільна частота.
    // Драйвер сам порахує дільник: 40 000 000 / (50 × 16 384) ≈ 48.8.
    timer.clk_cfg         = LEDC_USE_XTAL_CLK;

    if (ledc_timer_config(&timer) != ESP_OK) {
        Serial.println("LEDC timer configuration failed");
        while (true) delay(1000);  // Без таймера далі немає сенсу
    }

    // --- Крок 2. Налаштувати МУЗИКАНТА (канал) і підключити до піна ----------
    ledc_channel_config_t channel = {};
    channel.gpio_num   = SERVO_PIN;          // Вихід на GPIO4
    channel.speed_mode = SERVO_MODE;         // Та сама група, що й у таймера
    channel.channel    = SERVO_CHANNEL;      // Канал №0
    channel.timer_sel  = SERVO_TIMER;        // Під який метроном грати: таймер №0
    channel.intr_type  = LEDC_INTR_DISABLE;  // Переривання не потрібні — усе робить апаратура
    channel.duty       = angleToDuty(90);    // Стартове положення — центр
    channel.hpoint     = 0;                  // HIGH починається на поділці 0 (на початку періоду)

    if (ledc_channel_config(&channel) != ESP_OK) {
        Serial.println("LEDC channel configuration failed");
        while (true) delay(1000);
    }

    // Перевірка: яку частоту таймер видає насправді.
    Serial.printf("LEDC Timer 0 freq=%lu Hz\n",
                  static_cast<unsigned long>(ledc_get_freq(SERVO_MODE, SERVO_TIMER)));
}

// ============================================================================
// 6. loop() — як у Servo 01
// ============================================================================
void loop() {
    setServoAngle(30);
    delay(1200);   // Процесор «спить», а LEDC шле імпульси сам
    setServoAngle(90);
    delay(1200);
    setServoAngle(150);
    delay(1200);
}