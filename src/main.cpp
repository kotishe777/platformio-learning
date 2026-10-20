/*
 * Mini-project 2 v2 — STM32 HAL: button controls LED
 * Board:  WeAct BlackPill STM32F411CEU6
 * LED:    PC13 (active low)
 * Button: PA0 KEY (to GND, internal pull-up)
 * Each press switches LED blink period: 500 ms <-> 100 ms.
 * Non-blocking timing with HAL_GetTick(), 30 ms debounce.
 * Flashed via WeAct HID bootloader: app at 0x08004000 (weact_hid_app.ld).
 */

#include "stm32f4xx_hal.h"

struct Config {
  static constexpr uint16_t ledPin       = GPIO_PIN_13;
  static constexpr uint16_t buttonPin    = GPIO_PIN_0;
  static constexpr uint32_t slowPeriodMs = 500;
  static constexpr uint32_t fastPeriodMs = 100;
  static constexpr uint32_t debounceMs   = 30;
};

namespace {

GPIO_TypeDef* const ledPort    = GPIOC;
GPIO_TypeDef* const buttonPort = GPIOA;

void initGpio() {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  HAL_GPIO_WritePin(ledPort, Config::ledPin, GPIO_PIN_SET);

  GPIO_InitTypeDef led{};
  led.Pin   = Config::ledPin;
  led.Mode  = GPIO_MODE_OUTPUT_PP;
  led.Pull  = GPIO_NOPULL;
  led.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ledPort, &led);

  GPIO_InitTypeDef button{};
  button.Pin  = Config::buttonPin;
  button.Mode = GPIO_MODE_INPUT;
  button.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(buttonPort, &button);
}

bool isButtonDown() {
  return HAL_GPIO_ReadPin(buttonPort, Config::buttonPin) == GPIO_PIN_RESET;
}

}

extern "C" void SysTick_Handler() {
  HAL_IncTick();
}

int main() {
  SystemCoreClockUpdate();
  HAL_Init();
  __enable_irq();
  initGpio();

  uint32_t period     = Config::slowPeriodMs;
  uint32_t lastToggle = HAL_GetTick();
  uint32_t lastChange = HAL_GetTick();
  bool     rawDown    = false;
  bool     stableDown = false;

  while (true) {
    const uint32_t now  = HAL_GetTick();
    const bool     down = isButtonDown();

    if (down != rawDown) {
      rawDown    = down;
      lastChange = now;
    }

    if (rawDown != stableDown && now - lastChange >= Config::debounceMs) {
      stableDown = rawDown;
      if (stableDown) {
        period = (period == Config::slowPeriodMs) ? Config::fastPeriodMs : Config::slowPeriodMs;
      }
    }

    if (now - lastToggle >= period) {
      HAL_GPIO_TogglePin(ledPort, Config::ledPin);
      lastToggle = now;
    }
  }
}