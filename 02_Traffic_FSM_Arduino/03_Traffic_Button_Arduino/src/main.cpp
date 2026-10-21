#include <Arduino.h>

// Урок 3. FSM + зовнішня подія від кнопки.
// Умови: зелений триває 8 с. Перше натискання ПІД ЧАС GREEN
// скорочує ЗАГАЛЬНУ тривалість цієї зеленої фази до 6 с.
// Якщо натиснути після 6-ї секунди, GREEN закінчиться на цій ітерації.
// Одна подія за фазу. Часу самого натискання ми не віднімаємо 2 с.

constexpr uint8_t RED_LED = 16;
constexpr uint8_t GREEN_LED = 15;
constexpr uint8_t YELLOW_LED = 4;
constexpr uint8_t BUTTON_PIN = 21; // Кнопка між GPIO21 та GND.
constexpr uint32_t RED_MS = 5000;
constexpr uint32_t GREEN_MS = 8000;
constexpr uint32_t GREEN_SHORT_MS = 6000;
constexpr uint32_t YELLOW_MS = 2000;
constexpr uint32_t DEBOUNCE_MS = 30;

enum class State { Red, Green, Yellow };
State state = State::Red;
uint32_t enteredAt = 0;
uint32_t greenDuration = GREEN_MS;
bool pressUsedInThisGreen = false;

// Умова "натиснули" — подія, а не стан світлофора.
// Фільтрація механічного брязкоту контакту прихована тут,
// щоб не змішувати FSM кнопки з FSM світлофора в одному switch.
class Button {
public:
    bool pressedEvent(uint32_t now) {
        const bool rawPressed = (digitalRead(BUTTON_PIN) == LOW);
        if (rawPressed != lastRaw_) {
            lastRaw_ = rawPressed;
            changedAt_ = now;        // Починається перевірка нового рівня.
        }
        if (now - changedAt_ >= DEBOUNCE_MS && rawPressed != stablePressed_) {
            stablePressed_ = rawPressed; // Підтверджена зміна рівня.
            return stablePressed_;      // true лише на СТАБІЛЬНОМУ натисканні.
        }
        return false;
    }
private:
    bool lastRaw_ = false;
    bool stablePressed_ = false;
    uint32_t changedAt_ = 0;
};
Button button;

void applyOutputs(State s) {
    digitalWrite(RED_LED,    s == State::Red    ? HIGH : LOW);
    digitalWrite(GREEN_LED,  s == State::Green  ? HIGH : LOW);
    digitalWrite(YELLOW_LED, s == State::Yellow ? HIGH : LOW);
}

// Вхід у GREEN скидає тільки параметри НОВОЇ зеленої фази.
// Завдяки цьому кнопка може вплинути один раз у кожному циклі.
void changeState(State next, uint32_t now) {
    state = next;
    enteredAt = now;
    if (state == State::Green) {
        greenDuration = GREEN_MS;
        pressUsedInThisGreen = false;
    }
    applyOutputs(state);
}

void setup() {
    pinMode(RED_LED, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);
    pinMode(YELLOW_LED, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    changeState(State::Red, millis());
}

void loop() {
    const uint32_t now = millis();
    const bool pressed = button.pressedEvent(now);

    // EVENT: натиснули кнопку. GUARD: зараз GREEN і ще не скорочували.
    // ACTION: змінити тривалість; поточний стан залишається GREEN.
    if (pressed && state == State::Green && !pressUsedInThisGreen) {
        greenDuration = GREEN_SHORT_MS;
        pressUsedInThisGreen = true;
    }

    const uint32_t elapsed = now - enteredAt;
    switch (state) {
        case State::Red:
            if (elapsed >= RED_MS) changeState(State::Green, now);
            break;
        case State::Green:
            if (elapsed >= greenDuration) changeState(State::Yellow, now);
            break;
        case State::Yellow:
            if (elapsed >= YELLOW_MS) changeState(State::Red, now);
            break;
    }
}
