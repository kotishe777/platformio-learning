/*
 * Module 3 mini-project — refactored LDR -> servo
 * Board: ESP32-S3 DevKitC-1 N16R8, framework ESP-IDF 5.5
 * LDR: 3V3 -> LDR -> GPIO9 (ADC1_CH8) -> 10k -> GND
 * Servo SG90: signal GPIO14, +5V separate supply, common GND, 100 uF cap
 * Light 50..3000 mV maps to 10..170 deg; full scale 0..180 deg = 500..2500 us
 * SMA over 8 samples, fixed 100 ms period, one log line per update
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ldr_sensor.hpp"
#include "moving_average.hpp"
#include "range.hpp"
#include "servo.hpp"

struct Config {
  static constexpr adc_unit_t    adcUnit    = ADC_UNIT_1;
  static constexpr adc_channel_t ldrChannel = ADC_CHANNEL_8;
  static constexpr ServoConfig   servo{GPIO_NUM_14, LEDC_TIMER_0, LEDC_CHANNEL_0,
                                       10, 170,
                                       500, 2500};
  static constexpr int           lightMinMv = 50;
  static constexpr int           lightMaxMv = 3000;
  static constexpr size_t        smaWindow  = 8;
  static constexpr uint32_t      periodMs   = 100;
};

static_assert(Config::lightMinMv < Config::lightMaxMv, "Light range must not be empty");
static_assert(Config::servo.minDeg < Config::servo.maxDeg, "Servo range must not be empty");

namespace {
constexpr const char* tag = "refactor";
}

extern "C" void app_main() {
  LightSensor ldr(Config::adcUnit, Config::ldrChannel);
  ldr.init();

  Servo servo(Config::servo);
  servo.init((servo.minDeg() + servo.maxDeg()) / 2);

  MovingAverage<Config::smaWindow> sma;
  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    const int mv     = ldr.readMv();
    const int avgMv  = sma.add(mv);
    const int target = mapRange(avgMv, Config::lightMinMv, Config::lightMaxMv,
                                servo.minDeg(), servo.maxDeg());
    const int angle  = servo.setAngle(target);

    ESP_LOGI(tag, "light=%4d mV  avg=%4d mV  ->  angle=%3d deg", mv, avgMv, angle);

    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::periodMs));
  }
}
