#include <Arduino.h>

// Урок 2. Світлофор як FSM із трьома станами.
// Основна мета — простежити: СТАН → GUARD → ПЕРЕХІД → ENTRY ACTION.
// Плата ESP32-S3, LED активні HIGH.

constexpr uint8_t RED_LED = 16;
constexpr uint8_t GREEN_LED = 15;
constexpr uint8_t YELLOW_LED = 4;

constexpr uint32_t RED_MS = 5000;
constexpr uint32_t GREEN_MS = 7000;
constexpr uint32_t YELLOW_MS = 2000;

enum class State { Red, Green, Yellow };

State currentState = State::Red;   // Автомат пам'ятає поточний режим.
uint32_t stateEnteredAt = 0;      // Час ПОЧАТКУ поточного стану.

// Виходи визначаються лише currentState (Moore-підхід).
// Викликаємо під час входу в стан, не щокожної ітерації loop().
void applyOutputs(State state) {
    digitalWrite(RED_LED,    state == State::Red    ? HIGH : LOW);
    digitalWrite(GREEN_LED,  state == State::Green  ? HIGH : LOW);
    digitalWrite(YELLOW_LED, state == State::Yellow ? HIGH : LOW);
}

// Тривалість — властивість стану. Повертаємо її окремою функцією,
// щоб у логіці переходів не дублювати константи.
uint32_t durationFor(State state) {
    switch (state) {
        case State::Red:    return RED_MS;
        case State::Green:  return GREEN_MS;
        case State::Yellow: return YELLOW_MS;
    }
    return RED_MS;
}

// Це правило графа станів: RED → GREEN → YELLOW → RED.
State nextState(State state) {
    switch (state) {
        case State::Red:    return State::Green;
        case State::Green:  return State::Yellow;
        case State::Yellow: return State::Red;
    }
    return State::Red;
}

// ЄДИНЕ місце переходу. Тут виконуємо ENTRY ACTION:
// 1) запам'ятовуємо новий стан;
// 2) починаємо відлік часу саме цього стану;
// 3) установлюємо виходи.
void changeState(State next, uint32_t now) {
    currentState = next;
    stateEnteredAt = now;
    applyOutputs(currentState);
}

void setup() {
    pinMode(RED_LED, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);
    pinMode(YELLOW_LED, OUTPUT);

    // ВАЖЛИВО: стартовий стан теж має бути застосований до LED.
    changeState(State::Red, millis());
}

void loop() {
    const uint32_t now = millis();
    const uint32_t elapsed = now - stateEnteredAt;

    // GUARD: завершився час поточного стану?
    // Якщо ні, нічого не змінюємо: FSM залишається у currentState.
    if (elapsed >= durationFor(currentState)) {
        changeState(nextState(currentState), now);
    }

    // Ключове: stateEnteredAt тут НЕ переприсвоюємо!
    // Інакше elapsed буде близьким до нуля в кожному циклі.
}
