// src/board_config.h — приховує різницю між платами
//
// Навіщо цей файл: один і той самий main.cpp можна зібрати і для ESP32,
// і для STM32. Усе, що відрізняється між платами (номери пінів, атрибути),
// зібрано тут, а main.cpp просто підключає цей файл.
#pragma once          // «Підключити лише один раз», навіть якщо #include трапиться двічі
#include <Arduino.h>

// #if ... #elif ... #endif — умовна компіляція:
// компілятор сам перевіряє, для якої плати збираємо, і бере лише потрібний блок.
#if defined(ARDUINO_ARCH_ESP32)
  #define ISR_ATTR IRAM_ATTR  // ESP32: код переривання кладемо у швидку внутрішню RAM
  const int LED  = 15;        // Червоний LED (в уроці був GPIO2)
  const int LED2 = 16;        // Синій LED (у цьому уроці не використовується)
#elif defined(ARDUINO_ARCH_STM32)
  #define ISR_ATTR            // STM32: спеціальний атрибут не потрібен
  const int LED  = PB0;
  const int LED2 = PB1;
#endif