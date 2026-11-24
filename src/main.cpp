/*
 * Module 4.1 hometask — UART link ESP32-S3 <-> STM32F411, one source for both boards
 * ESP32-S3 DevKitC-1 (env:esp32): UART1 TX GPIO17 / RX GPIO18, button GPIO6, LED GPIO15
 * STM32F411 BlackPill (env:stm32): USART2 TX PA2 / RX PA3, KEY PA0, LED PC13 (active low)
 * Wiring: GPIO17 -> PA3, PA2 -> GPIO18, GND <-> GND; 9600 8N1 on both sides
 * Button press sends 'T' to the other board, which toggles its LED
 * Board-specific pins are chosen with ARDUINO_ARCH_* macros; framework Arduino; no delay()
 */
#include <Arduino.h>

struct Link {
  static constexpr uint32_t baud       = 9600;
  static constexpr char     toggleCmd  = 'T';
  static constexpr uint32_t debounceMs = 30;
};

#if defined(ARDUINO_ARCH_ESP32)

struct Board {
  static constexpr const char* name         = "ESP32";
  static constexpr const char* peer         = "STM32";
  static constexpr uint32_t    uartRxPin    = 18;
  static constexpr uint32_t    uartTxPin    = 17;
  static constexpr uint32_t    buttonPin    = 6;
  static constexpr uint32_t    ledPin       = 15;
  static constexpr bool        ledActiveLow = false;
};

HardwareSerial peerUart(1);

void beginPeerUart() {
  peerUart.begin(Link::baud, SERIAL_8N1, Board::uartRxPin, Board::uartTxPin);
}

#elif defined(ARDUINO_ARCH_STM32)

struct Board {
  static constexpr const char* name         = "STM32";
  static constexpr const char* peer         = "ESP32";
  static constexpr uint32_t    uartRxPin    = PA3;
  static constexpr uint32_t    uartTxPin    = PA2;
  static constexpr uint32_t    buttonPin    = PA0;
  static constexpr uint32_t    ledPin       = PC13;
  static constexpr bool        ledActiveLow = true;
};

Uart peerUart(Board::uartRxPin, Board::uartTxPin);

void beginPeerUart() {
  peerUart.begin(Link::baud, SERIAL_8N1);
}

#else
#error "Unsupported board: only ESP32 and STM32 are supported"
#endif

class DebouncedButton {
 public:
  explicit DebouncedButton(uint32_t pin) : pin_(pin) {}

  void begin() {
    pinMode(pin_, INPUT_PULLUP);
  }

  bool pressed(uint32_t nowMs) {
    const bool raw = digitalRead(pin_) == LOW;
    if (raw != raw_) {
      raw_       = raw;
      changedMs_ = nowMs;
    }
    if (nowMs - changedMs_ >= Link::debounceMs && raw_ != stable_) {
      stable_ = raw_;
      return stable_;
    }
    return false;
  }

 private:
  uint32_t pin_;
  bool     raw_       = false;
  bool     stable_    = false;
  uint32_t changedMs_ = 0;
};

DebouncedButton button(Board::buttonPin);
bool            ledOn = false;

void setLed(bool on) {
  digitalWrite(Board::ledPin, on != Board::ledActiveLow ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  beginPeerUart();
  button.begin();
  pinMode(Board::ledPin, OUTPUT);
  setLed(false);
  Serial.print(Board::name);
  Serial.println(" UART ready: button -> peer LED, peer button -> this LED");
}

void loop() {
  const uint32_t now = millis();

  if (button.pressed(now)) {
    peerUart.write(Link::toggleCmd);
    Serial.print("Button -> sent 'T' to ");
    Serial.println(Board::peer);
  }

  while (peerUart.available() > 0) {
    const int received = peerUart.read();
    if (received == Link::toggleCmd) {
      ledOn = !ledOn;
      setLed(ledOn);
      Serial.print("Received 'T' from ");
      Serial.print(Board::peer);
      Serial.print(" -> LED ");
      Serial.println(ledOn ? "ON" : "OFF");
    } else {
      Serial.print("Ignored byte 0x");
      Serial.println(received, HEX);
    }
  }
}
