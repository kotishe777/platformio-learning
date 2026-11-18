/*
 * Module 3 mini-project — part 1: servo control with KY-040 encoder
 * Board: ESP32-S3 DevKitC-1 N16R8, framework ESP-IDF 5.5
 * Encoder: CLK GPIO4, DT GPIO5, + 3V3; button: GPIO6 -> button -> GND (internal pull-up)
 * Servo SG90 on GPIO14 (separate 5V, common GND); buzzer GPIO17 -> 220R -> module S
 * One click = 10 deg (short press toggles 10 <-> 5 deg), limits 10..170 deg
 * Turning past a limit beeps; long press (1 s) centers the servo
 * Non-blocking 10 ms loop, all modules shared from ../common
 */
#include "button.hpp"
#include "buzzer.hpp"
#include "encoder.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "servo.hpp"

struct Config {
  static constexpr ServoConfig    servo{GPIO_NUM_14, LEDC_TIMER_0, LEDC_CHANNEL_0,
                                        10, 170, 500, 2500};
  static constexpr gpio_num_t     encoderA      = GPIO_NUM_4;
  static constexpr gpio_num_t     encoderB      = GPIO_NUM_5;
  static constexpr gpio_num_t     buttonPin     = GPIO_NUM_6;
  static constexpr int            direction     = 1;
  static constexpr gpio_num_t     buzzerPin     = GPIO_NUM_17;
  static constexpr ledc_timer_t   buzzerTimer   = LEDC_TIMER_1;
  static constexpr ledc_channel_t buzzerChannel = LEDC_CHANNEL_1;
  static constexpr int            coarseStepDeg = 10;
  static constexpr int            fineStepDeg   = coarseStepDeg / 2;
  static constexpr uint16_t       limitBeepHz   = 2000;
  static constexpr uint16_t       limitBeepMs   = 80;
  static constexpr uint16_t       centerBeepHz  = 1000;
  static constexpr uint16_t       centerBeepMs  = 150;
  static constexpr uint32_t       periodMs      = 10;
};

static_assert(Config::fineStepDeg > 0, "Fine step must be positive");
static_assert(Config::direction == 1 || Config::direction == -1, "Direction must be 1 or -1");

namespace {
constexpr const char* tag = "servo_enc";

uint32_t nowMs() {
  return pdTICKS_TO_MS(xTaskGetTickCount());
}

void handleClick(int sign, int stepDeg, Servo& servo, Buzzer& buzzer, uint32_t now) {
  const bool atLimit = (sign > 0 && servo.angle() == servo.maxDeg()) ||
                       (sign < 0 && servo.angle() == servo.minDeg());
  if (atLimit) {
    buzzer.beep(Config::limitBeepHz, Config::limitBeepMs, now);
    ESP_LOGW(tag, "limit %d deg: cannot turn further", servo.angle());
    return;
  }
  const int angle = servo.setAngle(servo.angle() + sign * stepDeg);
  ESP_LOGI(tag, "click %+d -> angle %3d deg (step %d)", sign, angle, stepDeg);
}
}  // namespace

extern "C" void app_main() {
  Servo servo(Config::servo);
  const int center = (servo.minDeg() + servo.maxDeg()) / 2;
  servo.init(center);

  Encoder encoder(Config::encoderA, Config::encoderB);
  encoder.init();

  Button button(Config::buttonPin);
  button.init();

  Buzzer buzzer(Config::buzzerPin, Config::buzzerTimer, Config::buzzerChannel);
  buzzer.init();

  int stepDeg = Config::coarseStepDeg;
  ESP_LOGI(tag, "Ready: angle %d deg, step %d deg", servo.angle(), stepDeg);

  TickType_t lastWake = xTaskGetTickCount();

  while (true) {
    const uint32_t now = nowMs();

    const int clicks = encoder.readClicks() * Config::direction;
    const int sign   = clicks > 0 ? 1 : -1;
    for (int i = 0; i < clicks * sign; ++i) {
      handleClick(sign, stepDeg, servo, buzzer, now);
    }

    switch (button.update(now)) {
      case ButtonEvent::Short:
        stepDeg = stepDeg == Config::coarseStepDeg ? Config::fineStepDeg : Config::coarseStepDeg;
        ESP_LOGI(tag, "step -> %d deg", stepDeg);
        break;
      case ButtonEvent::Long:
        servo.setAngle(center);
        buzzer.beep(Config::centerBeepHz, Config::centerBeepMs, now);
        ESP_LOGI(tag, "long press -> center %d deg", center);
        break;
      case ButtonEvent::None:
        break;
    }

    buzzer.update(now);

    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::periodMs));
  }
}
