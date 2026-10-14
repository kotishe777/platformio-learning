#include <cstdint>
#include <cinttypes>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// constexpr — константа, відома під час компіляції; тут це тег і інтервал логування.
// tag — рядок-позначка джерела логу. uint32_t задає 32-бітний беззнаковий лічильник.
constexpr char tag[] = "hello_world";
constexpr uint32_t log_interval_ms = 1000;

// extern "C" зберігає ім'я app_main без C++ перетворення імен (name mangling):
// стартовий код ESP-IDF зможе знайти цю точку входу. Аргументів і результату немає.
// На відміну від setup()/loop(), app_main викликається один раз у задачі main FreeRTOS;
// повторення ми задаємо власним циклом. Створювати ще одну задачу тут не потрібно.
extern "C" void app_main()
{
    uint32_t counter = 0;

    while (true) {
        // ESP_LOGI — макрос логування рівня Info: тег, формат повідомлення, значення для формату.
        // На відміну від Serial.println(), додає службовий префікс і може фільтруватися
        // налаштуваннями логування; це не об'єкт окремого UART. Результат не використовуємо.
        // PRIu32 з <cinttypes> задає правильний printf-формат для uint32_t;
        // сусідні літерали рядків C++ об'єднує, а % указує місце значення counter.
        ESP_LOGI(tag, "Hello from ESP-IDF! Counter: %" PRIu32, counter);
        ++counter;
        // vTaskDelay приймає кількість ticks планувальника, а не мілісекунди.
        // pdMS_TO_TICKS(ms) перетворює ms за CONFIG_FREERTOS_HZ: при 100 Hz tick = 10 ms.
        // Arduino delay(ms) приймає ms; тут аргумент має бути в ticks. Блокується
        // поточна FreeRTOS task, і CPU може виконувати інші задачі; результату немає.
        // Пауза залежить від фази tick та планування, тому це не точний апаратний таймінг.
        vTaskDelay(pdMS_TO_TICKS(log_interval_ms));
    }
}
