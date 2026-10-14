/*
 * Lesson 2.5 — Section 07: Task Watchdog (ESP32-S3, Arduino Core 3.x / IDF 5.x).
 * Board: ESP32-S3 DevKitC-1 (N16R8), Arduino Core 3.x (pioarduino).
 * loopTask feeds the TWDT every 500 ms; send 'h' to hang the loop.
 * After WDT_TIMEOUT_MS the TWDT panics and resets; reset reason becomes 6.
 *
 * ============================================================================
 * ПІДКЛЮЧЕННЯ
 * ============================================================================
 *   Лише USB-кабель: ESP32 USB -> комп'ютер.
 *   Команда «зависнути»: надрукувати літеру h у Serial Monitor.
 */

// ============================================================================
// 1. ПІДКЛЮЧЕННЯ БІБЛІОТЕК
// ============================================================================

#include "board_config.h"   // Наш файл плати; всередині підключає Arduino.h
#include "esp_system.h"     // esp_reset_reason() — «чому плата перезапустилась»
#include "esp_task_wdt.h"   // Task Watchdog Timer (TWDT) — сторож для задач
#include "esp_err.h"        // esp_err_t, esp_err_to_name() — коди помилок ESP-IDF

// ============================================================================
// 2. НАЛАШТУВАННЯ
// ============================================================================

// Скільки мілісекунд пес чекає «кістку», перш ніж перезапустити плату.
// 🧪 Експеримент (і частина ДЗ 2.5): спробуй 10000 = 10 с.
constexpr uint32_t WDT_TIMEOUT_MS = 3000;

// ============================================================================
// 3. ГЛОБАЛЬНІ ЗМІННІ
// ============================================================================

// Чи вдалося налаштувати сторожа. Якщо ні — не годуємо (нема кого).
bool wdtReady = false;

// Причина останнього перезапуску. Читаємо один раз у setup(),
// а друкуємо в кожному рядку «Живий...», щоб не загубити після перезапуску.
int resetReason = 0;

// ============================================================================
// 4. setup()
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);   // Даємо монітору час під'єднатися через рідний USB

  // esp_reset_reason() — число-«записка», чому плата стартувала:
  //   1 = POWERON (увімкнули живлення), 3 = SW (програмний перезапуск),
  //   4 = PANIC (аварія), 6 = TASK_WDT (спрацював наш сторож), ...
  resetReason = static_cast<int>(esp_reset_reason());
  Serial.printf("Причина старту: %d (ESP_RST_TASK_WDT=%d)\n",
                resetReason, static_cast<int>(ESP_RST_TASK_WDT));

  // --- Крок 1. Налаштування сторожа ---------------------------------------
  esp_task_wdt_config_t config = {};
  config.timeout_ms     = WDT_TIMEOUT_MS;
  // idle_core_mask = 0: НЕ стежити за службовими задачами Idle на ядрах 0 і 1.
  // Нас цікавить лише наша задача loopTask (у ній крутиться loop()).
  config.idle_core_mask = 0;
  // trigger_panic = true: коли пес «гавкне» — аварійна зупинка й перезапуск.
  // (false = лише надрукувати попередження й працювати далі.)
  config.trigger_panic  = true;

  // Arduino Core 3.x часто ВЖЕ запустив сторожа ще до нашого setup().
  // Ініціалізувати вдруге не можна (буде помилка), тому перевіряємо:
  //   ESP_ERR_INVALID_STATE = сторожа ще немає -> init (створити);
  //   інакше                = сторож уже є    -> reconfigure (переналаштувати).
  // «умова ? A : B» — коротке «якщо умова, то A, інакше B».
  const esp_err_t initStatus = esp_task_wdt_status(nullptr);
  esp_err_t err = (initStatus == ESP_ERR_INVALID_STATE)
                      ? esp_task_wdt_init(&config)
                      : esp_task_wdt_reconfigure(&config);
  if (err != ESP_OK) {
    Serial.printf("Помилка WDT init/config: %s\n", esp_err_to_name(err));
    return;
  }

  // --- Крок 2. Ставимо нашу задачу «на облік» у сторожа -------------------
  // esp_task_wdt_status(nullptr) питає про ПОТОЧНУ задачу (nullptr = «я»):
  //   ESP_ERR_NOT_FOUND = ще не на обліку -> додаємо;
  //   ESP_OK            = вже на обліку   -> нічого не робимо.
  const esp_err_t status = esp_task_wdt_status(nullptr);
  if (status == ESP_ERR_NOT_FOUND) {
    err = esp_task_wdt_add(nullptr);   // nullptr = поточна задача loopTask
  } else {
    err = status;
  }
  if (err != ESP_OK) {
    Serial.printf("Помилка WDT add: %s\n", esp_err_to_name(err));
    return;
  }

  wdtReady = true;
  Serial.printf("Task WDT: %lu мс. Надішли 'h' у Serial Monitor для зависання.\n",
                static_cast<unsigned long>(WDT_TIMEOUT_MS));
}

// ============================================================================
// 5. loop()
// ============================================================================
void loop() {
  // «Кидаємо кістку»: таймер сторожа починає відлік з нуля.
  // Поки це відбувається частіше, ніж раз на WDT_TIMEOUT_MS — перезапуску не буде.
  if (wdtReady) esp_task_wdt_reset();

  // Чи прийшла з монітора літера 'h'?
  // Serial.available() — скільки символів чекає в буфері;
  // Serial.read()      — забрати один символ.
  if (Serial.available() && Serial.read() == 'h') {
    Serial.println("Зависання: більше не викликаємо esp_task_wdt_reset()...");
    Serial.flush();   // Дочекатися, поки текст справді відправиться в USB

    // Навмисне «зависання»: вічний порожній цикл. Кістки більше немає.
    // Через WDT_TIMEOUT_MS сторож спрацює -> panic -> перезапуск.
    while (true) { }
  }

  Serial.printf("Живий, годую сторожа... (причина старту: %d)\n", resetReason);
  delay(500);   // 500 мс < 3000 мс: годуємо із запасом
}