#include <Arduino.h>

constexpr uint8_t LED_RED  = 15;
constexpr uint8_t LED_BLUE = 16;
constexpr uint8_t BTN_EXT  = 4;
constexpr uint8_t BTN_BOOT = 0;

int mode = 1;

bool lastExt  = HIGH;
bool lastBoot = HIGH;

unsigned long tExt  = 0;
unsigned long tBoot = 0;

constexpr unsigned long DEBOUNCE_MS = 40;

unsigned long lastToggle = 0;

bool phase = false;

void applyLeds(int m, bool ph) {
  if (m == 1) {
    digitalWrite(LED_RED,  ph);
    digitalWrite(LED_BLUE, ph);
  } else if (m == 2) {
    digitalWrite(LED_RED,  ph);
    digitalWrite(LED_BLUE, !ph);
  } else {
    digitalWrite(LED_RED,  LOW);
    digitalWrite(LED_BLUE, LOW);
  }
}

void setup() {
  pinMode(LED_RED,  OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(BTN_EXT,  INPUT_PULLUP);
  pinMode(BTN_BOOT, INPUT);
}

void loop() {
  bool modeChanged = false;

  bool ext = digitalRead(BTN_EXT);
  if (ext != lastExt && millis() - tExt > DEBOUNCE_MS) {
    tExt = millis();
    lastExt = ext;
    if (ext == LOW) {
      mode = (mode == 1) ? 0 : 1;
      modeChanged = true;
    }
  }

  bool boot = digitalRead(BTN_BOOT);
  if (boot != lastBoot && millis() - tBoot > DEBOUNCE_MS) {
    tBoot = millis();
    lastBoot = boot;
    if (boot == LOW) {
      mode = (mode == 2) ? 0 : 2;
      modeChanged = true;
    }
  }

  if (modeChanged) {
    phase = true;
    lastToggle = millis();
    applyLeds(mode, phase);
  }

  if (mode != 0) {
    unsigned long interval = (mode == 1) ? 200 : 1000;
    if (millis() - lastToggle >= interval) {
      lastToggle = millis();
      phase = !phase;
      applyLeds(mode, phase);
    }
  }
}
