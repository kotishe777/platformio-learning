#include <atomic>
#include <cstdint>
#include <cstdio>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct Config {
  static constexpr gpio_num_t buttonPin   = GPIO_NUM_6;
  static constexpr gpio_num_t modePin     = GPIO_NUM_0;
  static constexpr gpio_num_t reactionPin = GPIO_NUM_15;

  static constexpr int64_t burstGapUs     = 100000;
  static constexpr int64_t timeDebounceUs = 50000;
  static constexpr int64_t settleUs       = 20000;
  static constexpr int64_t pollPeriodUs   = 10000;
  static constexpr int64_t confirmUs      = 50000;
  static constexpr int64_t longPressUs    = 2000000;

  static constexpr TickType_t waitTicks = pdMS_TO_TICKS(1) > 0 ? pdMS_TO_TICKS(1) : 1;
  static constexpr uint32_t   taskStack = 6144;
};

enum class Method : uint8_t { None, TimeBased, StateBased, Polling };
enum class ButtonEvent : uint8_t { None, Pressed, ShortPress, LongPress };

namespace {

struct IrqSnapshot {
  uint32_t count;
  int64_t  lastUs;
  int64_t  burstStartUs;
};

portMUX_TYPE       irqMux    = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t       labTask   = nullptr;
esp_timer_handle_t pollTimer = nullptr;
std::atomic<bool>  pollDue{false};

volatile uint32_t irqCount     = 0;
volatile int64_t  lastIrqUs    = 0;
volatile int64_t  burstStartUs = 0;

void IRAM_ATTR onButtonIsr(void*) {
  const int64_t now = esp_timer_get_time();

  portENTER_CRITICAL_ISR(&irqMux);
  if (now - lastIrqUs > Config::burstGapUs) {
    burstStartUs = now;
  }
  lastIrqUs = now;
  irqCount = irqCount + 1;
  portEXIT_CRITICAL_ISR(&irqMux);

  if (labTask != nullptr) {
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(labTask, &woken);
    portYIELD_FROM_ISR(woken);
  }
}

void onPollTimer(void*) {
  pollDue = true;
  if (labTask != nullptr) {
    xTaskNotifyGive(labTask);
  }
}

IrqSnapshot readIrqs() {
  portENTER_CRITICAL(&irqMux);
  const IrqSnapshot snap{irqCount, lastIrqUs, burstStartUs};
  portEXIT_CRITICAL(&irqMux);
  return snap;
}

void resetIrqs() {
  portENTER_CRITICAL(&irqMux);
  irqCount = 0;
  portEXIT_CRITICAL(&irqMux);
}

bool isLow(gpio_num_t pin) {
  return gpio_get_level(pin) == 0;
}

class ButtonFsm {
public:
  ButtonEvent tick(bool low, int64_t nowUs) {
    switch (state_) {
      case State::Idle:
        if (low) {
          enter(State::Pressed, nowUs);
          pressStartUs_ = nowUs;
        }
        break;

      case State::Pressed:
        if (!low) {
          enter(State::Idle, nowUs);
        } else if (nowUs - stateSinceUs_ >= Config::confirmUs) {
          enter(State::Held, nowUs);
          return ButtonEvent::Pressed;
        }
        break;

      case State::Held:
        if (!low) {
          heldUs_ = nowUs - pressStartUs_;
          enter(State::Release, nowUs);
          return heldUs_ < Config::longPressUs ? ButtonEvent::ShortPress : ButtonEvent::LongPress;
        }
        break;

      case State::Release:
        if (low) {
          stateSinceUs_ = nowUs;
        } else if (nowUs - stateSinceUs_ >= Config::confirmUs) {
          enter(State::Idle, nowUs);
        }
        break;
    }
    return ButtonEvent::None;
  }

  void reset() { state_ = State::Idle; }

  int64_t pressStartUs() const { return pressStartUs_; }
  int64_t heldUs() const { return heldUs_; }

private:
  enum class State : uint8_t { Idle, Pressed, Held, Release };

  void enter(State next, int64_t nowUs) {
    state_ = next;
    stateSinceUs_ = nowUs;
  }

  State   state_ = State::Idle;
  int64_t stateSinceUs_ = 0;
  int64_t pressStartUs_ = 0;
  int64_t heldUs_ = 0;
};

void configurePin(gpio_num_t pin, gpio_mode_t mode, bool pullUp, gpio_int_type_t interrupt) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.mode = mode;
  cfg.pull_up_en = pullUp ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = interrupt;
  ESP_ERROR_CHECK(gpio_config(&cfg));
}

class DebounceLab {
public:
  void init() {
    configurePin(Config::buttonPin, GPIO_MODE_INPUT, false, GPIO_INTR_NEGEDGE);
    configurePin(Config::modePin, GPIO_MODE_INPUT, true, GPIO_INTR_DISABLE);
    configurePin(Config::reactionPin, GPIO_MODE_OUTPUT, false, GPIO_INTR_DISABLE);
    gpio_set_level(Config::reactionPin, 0);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(Config::buttonPin, onButtonIsr, nullptr));

    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = onPollTimer;
    timerArgs.name = "poll_10ms";
    ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &pollTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(pollTimer, Config::pollPeriodUs));

    select(Method::None);
  }

  void update(int64_t nowUs) {
    if (pollDue.exchange(false)) {
      onPollTick(nowUs);
    }

    switch (method_) {
      case Method::None:       updateNone(); break;
      case Method::TimeBased:  updateTimeBased(); break;
      case Method::StateBased: updateStateBased(nowUs); break;
      case Method::Polling:    break;
    }
  }

private:
  static Method nextMethod(Method m) {
    switch (m) {
      case Method::None:       return Method::TimeBased;
      case Method::TimeBased:  return Method::StateBased;
      case Method::StateBased: return Method::Polling;
      default:                 return Method::None;
    }
  }

  static const char* methodName(Method m) {
    switch (m) {
      case Method::TimeBased:  return "TIME ";
      case Method::StateBased: return "STATE";
      case Method::Polling:    return "POLL ";
      default:                 return "NONE ";
    }
  }

  void select(Method method) {
    method_ = method;

    if (method_ == Method::Polling) {
      gpio_intr_disable(Config::buttonPin);
    } else {
      gpio_intr_enable(Config::buttonPin);
    }

    resetIrqs();
    seenIrq_ = 0;
    accepted_ = 0;
    pending_ = false;
    lastAcceptedUs_ = esp_timer_get_time() - Config::timeDebounceUs;
    buttonFsm_.reset();

    printf("\n=== Method: %s === (press BOOT for the next method)\n", methodName(method_));
  }

  bool takeNewIrqs(IrqSnapshot& snap) {
    snap = readIrqs();
    const bool hasNew = snap.count != seenIrq_;
    seenIrq_ = snap.count;
    return hasNew;
  }

  void react(int64_t startUs) {
    accepted_++;
    reactionLevel_ = !reactionLevel_;
    gpio_set_level(Config::reactionPin, reactionLevel_ ? 1 : 0);

    const int64_t nowUs = esp_timer_get_time();
    const bool pinHigh = !isLow(Config::buttonPin);

    printf("[%s] event #%3lu | raw IRQ %3lu | latency %7.2f ms | pin %s%s\n",
           methodName(method_),
           static_cast<unsigned long>(accepted_),
           static_cast<unsigned long>(seenIrq_),
           (nowUs - startUs) / 1000.0,
           pinHigh ? "HIGH" : "LOW ",
           pinHigh ? "  <- release" : "");
  }

  void updateNone() {
    const uint32_t before = seenIrq_;
    IrqSnapshot snap{};
    if (!takeNewIrqs(snap)) {
      return;
    }
    for (uint32_t i = before; i < snap.count; i++) {
      react(snap.burstStartUs);
    }
  }

  void updateTimeBased() {
    IrqSnapshot snap{};
    if (!takeNewIrqs(snap)) {
      return;
    }
    if (snap.lastUs - lastAcceptedUs_ < Config::timeDebounceUs) {
      return;
    }
    lastAcceptedUs_ = snap.lastUs;
    react(snap.burstStartUs);
  }

  void updateStateBased(int64_t nowUs) {
    IrqSnapshot snap{};
    if (takeNewIrqs(snap)) {
      pending_ = true;
      return;
    }
    if (!pending_ || nowUs - snap.lastUs < Config::settleUs) {
      return;
    }
    pending_ = false;

    if (isLow(Config::buttonPin)) {
      react(snap.burstStartUs);
    }
  }

  void onPollTick(int64_t nowUs) {
    if (modeButton_.tick(isLow(Config::modePin), nowUs) == ButtonEvent::Pressed) {
      select(nextMethod(method_));
      return;
    }

    if (method_ != Method::Polling) {
      return;
    }

    switch (buttonFsm_.tick(isLow(Config::buttonPin), nowUs)) {
      case ButtonEvent::Pressed:
        react(buttonFsm_.pressStartUs());
        break;
      case ButtonEvent::ShortPress:
        printf("[POLL ] SHORT PRESS (%lld ms)\n", static_cast<long long>(buttonFsm_.heldUs() / 1000));
        break;
      case ButtonEvent::LongPress:
        printf("[POLL ] LONG PRESS  (%lld ms)\n", static_cast<long long>(buttonFsm_.heldUs() / 1000));
        break;
      case ButtonEvent::None:
        break;
    }
  }

  Method   method_ = Method::None;
  uint32_t seenIrq_ = 0;
  uint32_t accepted_ = 0;
  bool     reactionLevel_ = false;

  int64_t lastAcceptedUs_ = 0;
  bool    pending_ = false;

  ButtonFsm buttonFsm_;
  ButtonFsm modeButton_;
};

DebounceLab lab;

void labTaskMain(void*) {
  lab.init();
  for (;;) {
    ulTaskNotifyTake(pdTRUE, Config::waitTicks);
    lab.update(esp_timer_get_time());
  }
}

}  // namespace

extern "C" void app_main() {
  xTaskCreate(labTaskMain, "debounce_lab", Config::taskStack, nullptr, 5, &labTask);
}
