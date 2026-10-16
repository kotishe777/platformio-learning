
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ; Зберігає ANSI-коди кольорів від ESP-IDF.
// monitor_filters = direct
// Дозволяємо всі рівні логування у цьому файлі.
// Важливо: визначити ДО підключення esp_log.h!
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"

// Назва модуля, яка відображається у логах.
static constexpr char TAG[] = "LOG_DEMO";

extern "C" void app_main()
{
    // Вмикаємо всі рівні для модуля LOG_DEMO.
    esp_log_level_set(TAG, ESP_LOG_VERBOSE);

    int counter = 0;

    while (true)
    {
        counter++;

        // ERROR — серйозна помилка.
        ESP_LOGE(TAG, "CRITICAL: Motor failure!");

        // WARNING — попередження.
        ESP_LOGW(TAG, "Temperature is too high!");

        // INFO — нормальна робота.
        ESP_LOGI(TAG, "System is running");

        // DEBUG — діагностична інформація.
        ESP_LOGD(TAG, "Counter = %d", counter);

        // VERBOSE — максимально докладна інформація.
        ESP_LOGV(TAG, "Loop iteration %d", counter);

        // Повторюємо кожні 3 секунди.
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}