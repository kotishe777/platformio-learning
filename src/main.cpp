#include <Arduino.h>

constexpr uint8_t  LDR_PIN   = 4;
constexpr uint8_t  RELAY_PIN = 5;

constexpr uint16_t THRESHOLD_DARK  = 1590;
constexpr uint16_t THRESHOLD_LIGHT = 2140;

constexpr uint32_t PERIOD_MS = 100;

bool relayOn = false;
uint32_t lastRead = 0;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
}

void loop() {
  if (millis() - lastRead < PERIOD_MS) {
    return;
  }
  lastRead = millis();

  uint16_t adc = analogRead(LDR_PIN);

  if (adc < THRESHOLD_DARK && !relayOn) {
    relayOn = true;
    digitalWrite(RELAY_PIN, HIGH);
  } else if (adc > THRESHOLD_LIGHT && relayOn) {
    relayOn = false;
    digitalWrite(RELAY_PIN, LOW);
  }

  Serial.printf("ADC: %4u | relay: %s\n", adc, relayOn ? "ON" : "OFF");
}
