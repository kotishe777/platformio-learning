#include <Arduino.h>

constexpr uint8_t  LDR_PIN   = 4;
constexpr uint32_t PERIOD_MS = 100;

constexpr float ADC_MAX  = 4095.0;
constexpr float U_REF_MV = 3100.0;

uint32_t lastRead = 0;
uint32_t sampleNumber = 0;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);

  Serial.println("   # |  RAW | U calc, mV | U meas, mV | error, %");
  Serial.println("-----+------+------------+------------+---------");
}

void loop() {
  if (millis() - lastRead >= PERIOD_MS) {
    lastRead = millis();
    sampleNumber++;

    uint16_t raw   = analogRead(LDR_PIN);
    float    uCalc = raw * U_REF_MV / ADC_MAX;
    uint32_t uMeas = analogReadMilliVolts(LDR_PIN);

    float error = (uMeas > 0) ? fabs(uCalc - uMeas) / uMeas * 100.0 : 0.0;

    Serial.printf("%4u | %4u | %10.1f | %10u | %7.2f\n",
                  sampleNumber, raw, uCalc, uMeas, error);
  }
}
