#include <Arduino.h>

constexpr uint8_t LDR_PIN    = 4;
constexpr uint8_t RELAY_PIN  = 5;
constexpr uint8_t BUTTON_PIN = 6;

constexpr uint16_t THRESHOLD_DARK  = 1590;
constexpr uint16_t THRESHOLD_LIGHT = 2140;

constexpr uint32_t PERIOD_MS   = 100;
constexpr uint32_t DEBOUNCE_MS = 40;

enum Mode : uint8_t {
  MODE_AUTO,
  MODE_FORCED_OFF,
  MODE_FORCED_ON
};

Mode mode = MODE_AUTO;
bool relayOn = false;

uint32_t lastRead = 0;
bool lastButton = HIGH;
uint32_t lastButtonChange = 0;

const char* modeName(Mode m) {
  switch (m) {
    case MODE_FORCED_OFF: return "FORCED OFF";
    case MODE_FORCED_ON:  return "FORCED ON ";
    default:              return "AUTO      ";
  }
}

void setRelay(bool on) {
  if (on != relayOn) {
    relayOn = on;
    digitalWrite(RELAY_PIN, on ? HIGH : LOW);
  }
}

void applyAuto(uint16_t adc) {
  if (adc < THRESHOLD_DARK) {
    setRelay(true);
  } else if (adc > THRESHOLD_LIGHT) {
    setRelay(false);
  }
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void loop() {
  bool button = digitalRead(BUTTON_PIN);
  if (button != lastButton && millis() - lastButtonChange > DEBOUNCE_MS) {
    lastButtonChange = millis();
    lastButton = button;

    if (button == LOW) {
      if (mode == MODE_AUTO) {
        mode = MODE_FORCED_OFF;
        setRelay(false);
      } else if (mode == MODE_FORCED_OFF) {
        mode = MODE_FORCED_ON;
        setRelay(true);
      } else {
        mode = MODE_AUTO;
        applyAuto(analogRead(LDR_PIN));
      }

      Serial.printf("Button pressed -> mode: %s\n", modeName(mode));
    }
  }

  if (millis() - lastRead >= PERIOD_MS) {
    lastRead = millis();

    uint16_t adc = analogRead(LDR_PIN);

    if (mode == MODE_AUTO) {
      applyAuto(adc);
    }

    Serial.printf("ADC: %4u | mode: %s | relay: %s\n",
                  adc, modeName(mode), relayOn ? "ON" : "OFF");
  }
}
