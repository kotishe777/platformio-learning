/*
 * Lesson 2.5 — Check frequency: CPU, APB, XTAL and hardware timer clocks.
 * Board: ESP32-S3 DevKitC-1 (N16R8), Arduino Core 3.x (pioarduino).
 * Sets CPU frequency, starts a 1 MHz hardware timer and prints all clocks.
 * Report repeats every 5 s so it is visible after USB monitor connects.
 *
 * ============================================================================
 * ПІДКЛЮЧЕННЯ
 * ============================================================================
 *   Лише USB-кабель: ESP32 USB -> комп'ютер.
 *   Жодних додаткових деталей не потрібно.
 */

// ============================================================================
// 1. ПІДКЛЮЧЕННЯ БІБЛІОТЕК
// ============================================================================

// Arduino.h — Serial, millis(), функції частот (getCpuFrequencyMhz тощо)
// і функції апаратного таймера (timerBegin, timerGetFrequency).
#include <Arduino.h>

// ============================================================================
// 2. НАЛАШТУВАННЯ (КОНСТАНТИ)
// ============================================================================

// Бажана частота процесора, МГц.
// Допустимі значення для ESP32-S3: 240, 160, 80.
// (Є ще 40 / 20 / 10, але нижче 80 МГц рідний USB перестає працювати,
//  і монітор замовкне — тому їх не пробуємо.)
// Спробуй 160 або 80 і подивись, що станеться з APB і Timer.
constexpr uint32_t CPU_FREQ_MHZ = 240;

// Частота тіків апаратного таймера: 1 000 000 Гц = 1 тік щомікросекунди.
// Таймер отримує 80 МГц від APB і ділить на 80: 80 000 000 / 80 = 1 000 000.
constexpr uint32_t TIMER_FREQ_HZ = 1000000;

// Як часто повторювати звіт у моніторі.
constexpr uint32_t REPORT_INTERVAL_MS = 5000;

// ============================================================================
// 3. ГЛОБАЛЬНІ ЗМІННІ
// ============================================================================

// Вказівник на апаратний таймер. nullptr = «таймера ще немає».
// timerBegin() поверне сюди «ручку», через яку ми звертаємось до таймера.
hw_timer_t* timer = nullptr;

// ============================================================================
// 4. ЗВІТ ПРО ЧАСТОТИ
// ============================================================================
void printClockReport() {
    Serial.println("--------------------------");

    // Частота процесора, МГц.
    Serial.printf("CPU:   %lu MHz\n",
                  static_cast<unsigned long>(getCpuFrequencyMhz()));

    // Частота шини периферії, Гц. Від неї тактуються таймери, UART, I2C, LEDC.
    Serial.printf("APB:   %lu Hz\n",
                  static_cast<unsigned long>(getApbFrequency()));

    // Частота кварцу на платі, МГц. Вона фізична й незмінна — «камертон».
    Serial.printf("XTAL:  %lu MHz\n",
                  static_cast<unsigned long>(getXtalFrequencyMhz()));

    // Реальна частота тіків нашого таймера.
    // Перевіряємо, що таймер створено, інакше звертання до nullptr «вбило» б програму.
    if (timer != nullptr) {
        Serial.printf("Timer: %lu Hz\n",
                      static_cast<unsigned long>(timerGetFrequency(timer)));
    }
}

// ============================================================================
// 5. setup() — один раз після старту
// ============================================================================
void setup() {
    Serial.begin(115200);

    // Пауза 1 с: даємо монітору час під'єднатися через рідний USB.
    delay(1000);

    // Запам'ятовуємо частоту процесора ДО зміни.
    const uint32_t cpuBefore = getCpuFrequencyMhz();

    // Просимо змінити частоту процесора. Повертає true, якщо вдалося.
    const bool cpuChanged = setCpuFrequencyMhz(CPU_FREQ_MHZ);

    Serial.println("==========================");
    Serial.printf("CPU before: %lu MHz\n", static_cast<unsigned long>(cpuBefore));
    Serial.printf("CPU after:  %lu MHz (%s)\n",
                  static_cast<unsigned long>(getCpuFrequencyMhz()),
                  cpuChanged ? "OK" : "FAILED");

    // Створюємо апаратний таймер із частотою тіків 1 МГц.
    // Core 3.x: драйвер сам обирає вільний таймер і рахує дільник.
    // Якщо частоту неможливо отримати, повертається nullptr.
    timer = timerBegin(TIMER_FREQ_HZ);
    if (timer == nullptr) {
        Serial.println("Timer initialization failed!");
    }

    printClockReport();
}

// ============================================================================
// 6. loop() — повторюємо звіт кожні 5 с (без delay, через millis)
// ============================================================================
void loop() {
    static uint32_t lastReportMs = 0;   // Коли востаннє друкували звіт
    const uint32_t now = millis();

    if (now - lastReportMs >= REPORT_INTERVAL_MS) {
        lastReportMs = now;
        printClockReport();
    }
}