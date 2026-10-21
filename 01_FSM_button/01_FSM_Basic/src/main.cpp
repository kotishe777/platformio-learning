#include <Arduino.h>

// Підключення:
// GPIO6 --- кнопка --- GND
// GPIO5 --- резистор 330 Ом --- анод LED; катод LED --- GND
// INPUT_PULLUP утримує вхід у HIGH, поки кнопка відпущена.
constexpr uint8_t BUTTON_PIN = 6;
constexpr uint8_t LED_PIN = 5;
constexpr uint32_t DEBOUNCE_MS = 25;

// Стан автомата — НЕ те саме, що миттєве значення GPIO.
// Під час перевірки натискання або відпускання автомат чекає,
// чи протримається новий рівень щонайменше DEBOUNCE_MS.
enum class ButtonState : uint8_t {
    Released,          // Стабільно відпущена
    DebouncingPress,   // LOW з'явився, але ще не підтверджений
    Pressed,           // Стабільно натиснута
    DebouncingRelease  // HIGH з'явився, але ще не підтверджений
};

ButtonState buttonState = ButtonState::Released;
uint32_t stateStartedAt = 0;  // Час початку перевірки нового рівня
uint32_t pressCount = 0;
bool ledOn = false;

// Повертає true рівно один раз на ПІДТВЕРДЖЕНЕ натискання.
// Тому виклик цієї функції у кожній ітерації loop() не додає
// кілька натискань, поки користувач утримує кнопку.
bool updateButton(uint32_t now) {
    const bool isPressed = (digitalRead(BUTTON_PIN) == LOW);

    switch (buttonState) {
        case ButtonState::Released:
            // Перша ознака натискання: починаємо відлік часу.
            if (isPressed) {
                buttonState = ButtonState::DebouncingPress;
                stateStartedAt = now;
            }
            break;

        case ButtonState::DebouncingPress:
            if (!isPressed) {
                // LOW не втримався: це міг бути брязкіт.
                buttonState = ButtonState::Released;
            } else if (now - stateStartedAt >= DEBOUNCE_MS) {
                // LOW безперервно тримається потрібний час.
                buttonState = ButtonState::Pressed;
                return true;  // ЄДИНА точка генерації натискання
            }
            break;

        case ButtonState::Pressed:
            // Поки LOW, нічого не генеруємо повторно.
            if (!isPressed) {
                buttonState = ButtonState::DebouncingRelease;
                stateStartedAt = now;
            }
            break;

        case ButtonState::DebouncingRelease:
            if (isPressed) {
                // Кнопка знову LOW: відпускання не підтвердилося.
                buttonState = ButtonState::Pressed;
            } else if (now - stateStartedAt >= DEBOUNCE_MS) {
                // HIGH втримався: дозволяємо наступне натискання.
                buttonState = ButtonState::Released;
            }
            break;
    }

    return false;
}

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.println("FSM debounce: кожне підтверджене натискання перемикає LED");
}

void loop() {
    // Передаємо один і той самий часовий знімок у FSM.
    const uint32_t now = millis();

    if (updateButton(now)) {
        ++pressCount;
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
        Serial.printf("Натискання #%lu; LED %s\n",
                      static_cast<unsigned long>(pressCount),
                      ledOn ? "ON" : "OFF");
    }
}
