/*
 * Module 3 mini-project — part 3: encoder calculator
 * Board: ESP32-S3 DevKitC-1 N16R8, framework ESP-IDF 5.5
 * Encoder: CLK GPIO4, DT GPIO5; button GPIO6 -> GND (internal pull-up); buzzer GPIO17
 * Turn = set first operand -> press -> choose + - * / -> press -> second operand -> press
 * Operands 0..999; long press cycles the turn step x1 / x10 / x100
 * Console format: 234 -> 234 + -> 234 + 77 -> 234 + 77 = 311 (fields updated with \b)
 * Division prints two decimals; division by zero prints an error
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "button.hpp"
#include "buzzer.hpp"
#include "encoder.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "range.hpp"

struct Config {
  static constexpr gpio_num_t     encoderA      = GPIO_NUM_4;
  static constexpr gpio_num_t     encoderB      = GPIO_NUM_5;
  static constexpr gpio_num_t     buttonPin     = GPIO_NUM_6;
  static constexpr int            direction     = 1;
  static constexpr gpio_num_t     buzzerPin     = GPIO_NUM_17;
  static constexpr ledc_timer_t   buzzerTimer   = LEDC_TIMER_1;
  static constexpr ledc_channel_t buzzerChannel = LEDC_CHANNEL_1;
  static constexpr int            maxOperand    = 999;
  static constexpr int            stepCount     = 3;
  static constexpr int            steps[stepCount] = {1, 10, 100};
  static constexpr uint16_t       confirmBeepHz = 1500;
  static constexpr uint16_t       confirmBeepMs = 40;
  static constexpr uint16_t       stepBeepHz    = 1000;
  static constexpr uint16_t       stepBeepMs    = 80;
  static constexpr uint32_t       periodMs      = 10;
};

static_assert(Config::maxOperand * 100 / 1 <= 1000000, "Scaled division must fit into int");

namespace {
constexpr Tone resultMelody[] = {{784, 100}, {1047, 200}};

enum class Op : uint8_t { Add, Sub, Mul, Div };
constexpr int         opCount         = 4;
constexpr const char* opText[opCount] = {"+ ", "- ", "* ", "/ "};

enum class CalcState : uint8_t { FirstOperand, Operator, SecondOperand, Result };
enum class PressResult : uint8_t { Confirmed, Done, NewCalculation };

uint32_t nowMs() {
  return pdTICKS_TO_MS(xTaskGetTickCount());
}

class Calculator {
 public:
  void start() {
    a_         = 0;
    b_         = 0;
    op_        = 0;
    stepIndex_ = 0;
    state_     = CalcState::FirstOperand;
    printf("\nCALC> ");
    newField(number(a_));
  }

  void onClicks(int clicks) {
    switch (state_) {
      case CalcState::FirstOperand:
        a_ = clampValue(a_ + clicks * step(), 0, Config::maxOperand);
        replaceField(number(a_));
        break;
      case CalcState::Operator:
        op_ = ((op_ + clicks) % opCount + opCount) % opCount;
        replaceField(opText[op_]);
        break;
      case CalcState::SecondOperand:
        b_ = clampValue(b_ + clicks * step(), 0, Config::maxOperand);
        replaceField(number(b_));
        break;
      case CalcState::Result:
        break;
    }
  }

  PressResult onShortPress() {
    switch (state_) {
      case CalcState::FirstOperand:
        putchar(' ');
        newField(opText[op_]);
        state_ = CalcState::Operator;
        return PressResult::Confirmed;
      case CalcState::Operator:
        newField(number(b_));
        state_ = CalcState::SecondOperand;
        return PressResult::Confirmed;
      case CalcState::SecondOperand:
        printResult();
        state_ = CalcState::Result;
        return PressResult::Done;
      case CalcState::Result:
      default:
        start();
        return PressResult::NewCalculation;
    }
  }

  bool onLongPress() {
    if (state_ != CalcState::FirstOperand && state_ != CalcState::SecondOperand) {
      return false;
    }
    stepIndex_ = (stepIndex_ + 1) % Config::stepCount;
    printf("\n[step x%d]\n", step());
    redraw();
    return true;
  }

 private:
  int step() const {
    return Config::steps[stepIndex_];
  }

  const char* number(int value) {
    snprintf(text_, sizeof(text_), "%d", value);
    return text_;
  }

  void newField(const char* text) {
    fputs(text, stdout);
    fieldLen_ = static_cast<int>(strlen(text));
    fflush(stdout);
  }

  void replaceField(const char* text) {
    const int len = static_cast<int>(strlen(text));
    for (int i = 0; i < fieldLen_; ++i) {
      putchar('\b');
    }
    fputs(text, stdout);
    for (int i = len; i < fieldLen_; ++i) {
      putchar(' ');
    }
    for (int i = len; i < fieldLen_; ++i) {
      putchar('\b');
    }
    fieldLen_ = len;
    fflush(stdout);
  }

  void redraw() {
    printf("CALC> ");
    if (state_ == CalcState::FirstOperand) {
      newField(number(a_));
      return;
    }
    printf("%d %s", a_, opText[op_]);
    newField(number(b_));
  }

  void printResult() {
    switch (static_cast<Op>(op_)) {
      case Op::Add:
        printf(" = %d\n", a_ + b_);
        break;
      case Op::Sub:
        printf(" = %d\n", a_ - b_);
        break;
      case Op::Mul:
        printf(" = %d\n", a_ * b_);
        break;
      case Op::Div:
        if (b_ == 0) {
          printf(" = error: division by zero\n");
        } else {
          const int scaled = a_ * 100 / b_;
          printf(" = %d.%02d\n", scaled / 100, scaled % 100);
        }
        break;
    }
    printf("Press the button for a new calculation.\n");
    fflush(stdout);
  }

  int       a_         = 0;
  int       b_         = 0;
  int       op_        = 0;
  int       stepIndex_ = 0;
  int       fieldLen_  = 0;
  CalcState state_     = CalcState::FirstOperand;
  char      text_[12]  = {};
};
}  // namespace

extern "C" void app_main() {
  Encoder encoder(Config::encoderA, Config::encoderB);
  encoder.init();

  Button button(Config::buttonPin);
  button.init();

  Buzzer buzzer(Config::buzzerPin, Config::buzzerTimer, Config::buzzerChannel);
  buzzer.init();

  printf("\nCALCULATOR: turn = change, short press = confirm, long press = step x1/x10/x100\n");
  printf("Operands 0..%d, operations + - * /\n", Config::maxOperand);

  Calculator calc;
  calc.start();

  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    const uint32_t now    = nowMs();
    const int      clicks = encoder.readClicks() * Config::direction;
    if (clicks != 0) {
      calc.onClicks(clicks);
    }

    switch (button.update(now)) {
      case ButtonEvent::Short:
        if (calc.onShortPress() == PressResult::Done) {
          buzzer.play(resultMelody, sizeof(resultMelody) / sizeof(resultMelody[0]), now);
        } else {
          buzzer.beep(Config::confirmBeepHz, Config::confirmBeepMs, now);
        }
        break;
      case ButtonEvent::Long:
        if (calc.onLongPress()) {
          buzzer.beep(Config::stepBeepHz, Config::stepBeepMs, now);
        }
        break;
      case ButtonEvent::None:
        break;
    }

    buzzer.update(now);

    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::periodMs));
  }
}
