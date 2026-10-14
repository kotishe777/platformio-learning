// src/board_config.h — приховує різницю між платами
//
// Навіщо цей файл: один і той самий main.cpp можна зібрати і для ESP32,
// і для STM32. Усе, що відрізняється між платами, зібрано тут.
#pragma once          // «Підключити лише один раз»
#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)
  #define ISR_ATTR IRAM_ATTR  // ESP32: код переривання — у швидкій внутрішній RAM
  const int LED  = 15;        // Червоний LED (в уроці був GPIO2)
  const int LED2 = 16;        // Синій LED
#elif defined(ARDUINO_ARCH_STM32)
  #define ISR_ATTR            // STM32: атрибут не потрібен
  const int LED  = PB0;
  const int LED2 = PB1;
#endif