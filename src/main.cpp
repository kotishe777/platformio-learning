/*
 * Hometask 2.5: Timers. Exhaust fan control on ESP32-S3.
 * The fan (motor switched by a relay) turns on every period for a fixed run time,
 * then turns off automatically. Two hardware timers drive the whole cycle in their ISRs,
 * independent of loop(): a periodic timer turns the fan on, a one-shot timer turns it off.
 * LEDs mirror the state, Serial logs it, a watchdog guards loop().
 * Two modes, selected by TEST_MODE:
 *   true  - test mode: period 4 s, run 1 s (same 1:4 ratio, for quick checks)
 *   false - main mode: period 1 hour, run 15 minutes
 */
#include <Arduino.h>
#include "esp_task_wdt.h"

constexpr bool TEST_MODE = true;

constexpr uint64_t PERIOD_US = TEST_MODE ? 4000000ULL : 3600000000ULL;
constexpr uint64_t RUN_US    = TEST_MODE ? 1000000ULL : 900000000ULL;

constexpr uint16_t TIMER_DIVIDER = 80;
constexpr uint32_t WDT_TIMEOUT_S = 3;

constexpr uint8_t RELAY_PIN  = 4;
constexpr uint8_t LED_YELLOW = 5;
constexpr uint8_t LED_BLUE   = 6;

static_assert(RUN_US < PERIOD_US, "RUN_US must be shorter than PERIOD_US");

hw_timer_t *periodTimer = NULL;
hw_timer_t *runTimer    = NULL;

volatile bool     fanOn        = false;
volatile bool     stateChanged = false;
volatile uint32_t cycleNumber  = 0;

void IRAM_ATTR setFan(bool on) {
  fanOn = on;
  digitalWrite(RELAY_PIN,  on);
  digitalWrite(LED_YELLOW, on);
  digitalWrite(LED_BLUE,   !on);
  stateChanged = true;
}

void IRAM_ATTR onPeriodTimer() {
  if (fanOn) {
    return;
  }
  cycleNumber++;
  setFan(true);
  timerWrite(runTimer, 0);
  timerAlarmEnable(runTimer);
}

void IRAM_ATTR onRunTimer() {
  if (!fanOn) {
    return;
  }
  setFan(false);
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_BLUE,   OUTPUT);

  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  periodTimer = timerBegin(0, TIMER_DIVIDER, true);
  runTimer    = timerBegin(1, TIMER_DIVIDER, true);

  timerAttachInterrupt(periodTimer, &onPeriodTimer, false);
  timerAttachInterrupt(runTimer,    &onRunTimer,    false);

  timerAlarmWrite(periodTimer, PERIOD_US, true);
  timerAlarmWrite(runTimer,    RUN_US,    false);

  timerAlarmEnable(periodTimer);

  Serial.printf("Start | period: %llu ms | run: %llu ms\n", PERIOD_US / 1000, RUN_US / 1000);

  onPeriodTimer();
}

void loop() {
  esp_task_wdt_reset();

  if (stateChanged) {
    stateChanged = false;
    Serial.printf("%8lu ms | cycle #%u | fan %s\n",
                  millis(), cycleNumber, fanOn ? "ON" : "OFF");
  }

  // Uncomment to test the watchdog: loop() hangs and the board reboots in WDT_TIMEOUT_S
  // while (true) {}
}
