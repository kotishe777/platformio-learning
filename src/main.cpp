/*
 * Lesson 2.5 — PCNT frequency meter (Arduino Core 3.x / ESP-IDF 5.x).
 * Board: ESP32-S3 DevKitC-1 (N16R8), Arduino Core 3.x (pioarduino).
 * LEDC generates 10 kHz on GPIO5; PCNT counts rising edges on GPIO4.
 * Every ~1 s: stop, read, compute frequency, clear, restart.
 *
 * ============================================================================
 * СХЕМА ПІДКЛЮЧЕННЯ
 * ============================================================================
 *   1. ESP32 GPIO5 (вихід LEDC)  -> ESP32 GPIO4 (вхід PCNT)   — ОДИН дріт
 *   2. ESP32 USB                 -> комп'ютер
 *
 *   GPIO5 ----------------------> GPIO4
 *   (генератор 10 кГц)           (лічильник)
 *
 *   ВАЖЛИВО: від'єднайте від GPIO4 сигнал серво з попередніх уроків!
 *   До GPIO4 має бути під'єднаний лише цей дріт.
 *   Обидва піни працюють на 3.3 В — резистор не потрібен.
 */

// ============================================================================
// 1. ПІДКЛЮЧЕННЯ БІБЛІОТЕК
// ============================================================================

// Arduino.h — Serial, micros(), ledcAttach(), ledcWrite(), ledcReadFreq().
#include <Arduino.h>

// driver/pulse_cnt.h — драйвер лічильника імпульсів PCNT з ESP-IDF 5.x.
// (Це той самий pulse_cnt.h, що ви надсилали: «інструкція до лічильника».)
#include "driver/pulse_cnt.h"

// ============================================================================
// 2. НАЛАШТУВАННЯ
// ============================================================================

constexpr int      PWM_PIN   = 5;        // Вихід генератора (LEDC)
constexpr int      PCNT_PIN  = 4;        // Вхід лічильника (PCNT)
constexpr uint32_t SIGNAL_HZ = 10000;    // Частота тестового сигналу: 10 кГц
constexpr uint32_t WINDOW_US = 1000000;  // Вікно вимірювання: 1 000 000 мкс = 1 с

// ============================================================================
// 3. ГЛОБАЛЬНІ ЗМІННІ
// ============================================================================

// «Ручки» драйвера PCNT. nullptr = ще не створено.
// unit    — сам лічильник (регістр, у якому накопичується число);
// channel — «вхід» лічильника: який пін слухати і що робити на кожному фронті.
pcnt_unit_handle_t    pcnt_unit    = nullptr;
pcnt_channel_handle_t pcnt_channel = nullptr;

// Коли почалося поточне вікно вимірювання (мкс від старту).
uint32_t window_start_us = 0;

// ============================================================================
// 4. setup()
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);   // Даємо монітору час під'єднатися через рідний USB

    // --- Крок 1. Створюємо ЛІЧИЛЬНИК (unit) ---------------------------------
    // Межі рахунку: 16-бітний апаратний лічильник зі знаком -> -32768…32767.
    pcnt_unit_config_t unit_config = {};
    unit_config.low_limit  = -32768;
    unit_config.high_limit = 32767;

    // ESP_ERROR_CHECK(...) — «перевір результат; якщо помилка — зупини програму
    // з повідомленням і перезапусти плату». Зручно для навчальних прикладів.
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // --- Крок 2. Створюємо ВХІД лічильника (channel) ------------------------
    // edge_gpio_num  — пін, на якому ловимо фронти: GPIO4.
    // level_gpio_num — додатковий «керуючий» пін (наприклад, для напрямку обертання
    //                  енкодера). Нам не потрібен, тому -1 = «не використовується».
    pcnt_chan_config_t channel_config = {};
    channel_config.edge_gpio_num  = PCNT_PIN;
    channel_config.level_gpio_num = -1;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &channel_config, &pcnt_channel));

    // --- Крок 3. Що робити на кожному фронті -------------------------------
    // Фронт LOW -> HIGH (rising): INCREASE = додати +1.
    // Фронт HIGH -> LOW (falling): HOLD = нічого не робити.
    // Отже, один повний імпульс = рівно +1.
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
        pcnt_channel,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD));

    // --- Крок 4. Вмикаємо лічильник і обнуляємо ----------------------------
    // enable = «увімкнути живлення лічильника» (ще не рахує!).
    // clear  = «скинути на 0».
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));

    // --- Крок 5. Запускаємо ГЕНЕРАТОР на GPIO5 ------------------------------
    // ledcAttach(пін, частота, роздільність): 10 000 Гц, 8 біт (256 поділок).
    // ledcWrite(пін, 128): 128 з 256 = 50% — рівний меандр (половина HIGH, половина LOW).
    // || = «або»: якщо БУДЬ-ЯКА з двох функцій повернула false — помилка.
    if (!ledcAttach(PWM_PIN, SIGNAL_HZ, 8) ||
        !ledcWrite(PWM_PIN, 128)) {
        Serial.println("LEDC configuration failed");
        while (true) delay(1000);
    }

    // Яку частоту генератор видає насправді.
    Serial.printf("LEDC frequency: %lu Hz\n",
                  (unsigned long)ledcReadFreq(PWM_PIN));

    // --- Крок 6. СТАРТ рахунку і початок першого вікна ----------------------
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    window_start_us = micros();
}

// ============================================================================
// 5. loop()
// ============================================================================
void loop() {
    // Ще не минула 1 секунда? Виходимо — лічильник рахує сам, без нас.
    // (Віднімання беззнакових чисел працює правильно навіть після переповнення micros().)
    if (static_cast<uint32_t>(micros() - window_start_us) < WINDOW_US) {
        return;
    }

    // --- Секунда минула: «черговий підходить до турнікета» ------------------

    // 1) Зупиняємо лічильник, щоб число не змінювалося, поки ми його читаємо.
    //    З цього моменту починається коротка «сліпа зона».
    ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));

    // 2) Скільки часу НАСПРАВДІ тривало вікно (трохи більше за 1 000 000 мкс,
    //    бо loop() помічає кінець секунди не миттєво).
    const uint32_t elapsed_us = micros() - window_start_us;

    // 3) Читаємо, скільки імпульсів набралося.
    int pulses = 0;
    ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulses));

    // 4) Частота = імпульси / секунди.
    //    elapsed_us у мікросекундах, тож множимо на 1 000 000.
    //    1000000.0f — число з плаваючою комою (f = float), щоб не втратити дробову частину.
    const float measured_hz = pulses * 1000000.0f / elapsed_us;

    // 5) Скидаємо на 0, знову запускаємо й починаємо нове вікно.
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
    window_start_us = micros();

    // 6) Друкуємо результат. %.1f — один знак після коми.
    Serial.printf("Pulses: %d | Frequency: %.1f Hz\n",
                  pulses, measured_hz);
}