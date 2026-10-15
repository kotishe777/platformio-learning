#include <Arduino.h>

#define BUTTON_IN   16
#define EDGE_MODE   CHANGE
#define QUIET_MS    30

volatile uint32_t edgeCount   = 0;
volatile uint32_t firstEdgeUs = 0;
volatile uint32_t lastEdgeUs  = 0;

uint32_t eventNumber = 0;

void IRAM_ATTR button_isr() {
  uint32_t now = micros();
  if (edgeCount == 0) {
    firstEdgeUs = now;
  }
  lastEdgeUs = now;
  edgeCount++;
}

void setup() {
  pinMode(BUTTON_IN, INPUT_PULLUP);
  Serial.begin(115200);
  attachInterrupt(digitalPinToInterrupt(BUTTON_IN), button_isr, EDGE_MODE);
}

void loop() {
  noInterrupts();
  uint32_t count = edgeCount;
  uint32_t first = firstEdgeUs;
  uint32_t last  = lastEdgeUs;
  interrupts();

  if (count > 0 && (micros() - last) > QUIET_MS * 1000UL) {
    noInterrupts();
    edgeCount = 0;
    interrupts();

    eventNumber++;
    bool pressed = (digitalRead(BUTTON_IN) == LOW);
    float bounceMs = (last - first) / 1000.0;

    Serial.printf("Event #%u: %s | edges: %u | bounce duration: %.3f ms\n",
                  eventNumber, pressed ? "PRESS  " : "RELEASE", count, bounceMs);
  }

  delay(1);
}
