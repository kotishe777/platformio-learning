#pragma once

#include <Arduino.h>

// Та сама FSM із чотирма станами, але її дані ізольовані в об'єкті.
// Можна створити кілька незалежних кнопок без глобальних змінних.
class DebouncedButton {
public:
    enum class Event : uint8_t {
        None,       // За цей виклик підтвердженої події немає
        Pressed,    // Стабільний LOW підтверджений
        Released    // Стабільний HIGH підтверджений
    };

    explicit DebouncedButton(uint8_t pin, uint32_t debounceMs = 25)
        : pin_(pin), debounceMs_(debounceMs) {}

    // Фізична схема: кнопка між GPIO та GND.
    void begin() {
        pinMode(pin_, INPUT_PULLUP);
        state_ = State::Released;
        stateStartedAt_ = 0;
    }

    // Викликати якомога частіше з loop().
    // Повертає подію ЛИШЕ в момент підтвердженого переходу.
    Event update(uint32_t now) {
        const bool isPressed = (digitalRead(pin_) == LOW);

        switch (state_) {
            case State::Released:
                if (isPressed) {
                    state_ = State::DebouncingPress;
                    stateStartedAt_ = now;
                }
                break;

            case State::DebouncingPress:
                if (!isPressed) {
                    // Контакт повернувся до HIGH раніше порогу.
                    state_ = State::Released;
                } else if (now - stateStartedAt_ >= debounceMs_) {
                    state_ = State::Pressed;
                    return Event::Pressed;
                }
                break;

            case State::Pressed:
                if (!isPressed) {
                    state_ = State::DebouncingRelease;
                    stateStartedAt_ = now;
                }
                break;

            case State::DebouncingRelease:
                if (isPressed) {
                    // Відпускання не підтвердилося, повертаємося в Pressed.
                    state_ = State::Pressed;
                } else if (now - stateStartedAt_ >= debounceMs_) {
                    state_ = State::Released;
                    return Event::Released;
                }
                break;
        }

        return Event::None;
    }

private:
    enum class State : uint8_t {
        Released,
        DebouncingPress,
        Pressed,
        DebouncingRelease
    };

    const uint8_t pin_;
    const uint32_t debounceMs_;
    State state_ = State::Released;
    uint32_t stateStartedAt_ = 0;
};
