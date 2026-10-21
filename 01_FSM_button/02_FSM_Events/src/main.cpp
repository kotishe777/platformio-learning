#include <Arduino.h>
#include "DebouncedButton.h"

// Кнопка: GPIO6 -> кнопка -> GND.
// Світлодіод: GPIO5 -> 330 Ом -> анод LED; катод -> GND.
constexpr uint8_t BUTTON_PIN = 6;
constexpr uint8_t LED_PIN = 5;
constexpr uint32_t DEBOUNCE_MS = 25;

DebouncedButton button(BUTTON_PIN, DEBOUNCE_MS);
uint32_t pressCount = 0;

void setup() {
    Serial.begin(115200);
    button.begin();
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.println("FSM events: LED горить, лише поки кнопка підтверджено натиснута");
}

void loop() {
    // FSM працює з поточним станом GPIO та відміткою часу.
    // Порядок обробки подій визначається результатом update().
    const DebouncedButton::Event event = button.update(millis());

    switch (event) {
        case DebouncedButton::Event::Pressed:
            ++pressCount;
            digitalWrite(LED_PIN, HIGH);
            Serial.printf("PRESSED #%lu\n", static_cast<unsigned long>(pressCount));
            break;

        case DebouncedButton::Event::Released:
            digitalWrite(LED_PIN, LOW);
            Serial.println("RELEASED");
            break;

        case DebouncedButton::Event::None:
            // Нового підтвердженого фронту немає — нічого не робимо.
            break;
    }
}
