/*
 * Module 4.2 hometask — SSD1306 OLED 128x64 over I2C without a display library
 * Board: ESP32-S3 DevKitC-1, framework Arduino, Wire at 100 kHz (Standard mode)
 * Wiring: GPIO8 -> SDA, GPIO9 -> SCL, 3V3 -> VCC, GND -> GND, 4.7k pull-ups to 3V3
 * 1) I2C scanner prints every address that answers ACK (0x08..0x77)
 * 2) Init plan is sent command by command, each step is logged with ACK/ERROR
 * 3) A checkerboard is written to display RAM in 16-byte data chunks
 * 4) Every second the display is inverted (0xA7 / 0xA6) to show live commands
 * Control byte 0x00 = commands follow, 0x40 = data follow; no delay(), millis() only
 */
#include <Arduino.h>
#include <Wire.h>

// All settings in one place: pins, bus speed, display address and geometry
struct Config {
  static constexpr int sdaPin = 8;                 // I2C data line (SDA)
  static constexpr int sclPin = 9;                 // I2C clock line (SCL)
  static constexpr uint32_t i2cHz = 100000;        // 100 kHz is the safe breadboard speed
  static constexpr uint8_t oledAddress = 0x3C;     // 7-bit address (datasheet 8-bit: 0x78)
  static constexpr uint8_t width = 128;            // columns
  static constexpr uint8_t pages = 8;              // 64 rows / 8 rows per page
  static constexpr uint8_t chunkSize = 16;         // data bytes per I2C transaction
  static constexpr uint32_t serialWaitMs = 5000;   // wait for the monitor to open
  static constexpr uint32_t invertPeriodMs = 1000; // invert the screen once a second
};

// First byte after the address tells SSD1306 what follows
enum class ControlByte : uint8_t {
  Command = 0x00,  // Co = 0, D/C# = 0 -> a stream of commands
  Data = 0x40      // Co = 0, D/C# = 1 -> a stream of pixel bytes
};

// One step of the init plan: a command and an optional parameter
struct InitStep {
  const char* name;
  uint8_t bytes[2];
  uint8_t length;  // 1 = command only, 2 = command + parameter
};

// The init plan from the hometask, in the order it is sent
constexpr InitStep initPlan[] = {
  {"Display OFF", {0xAE, 0x00}, 1},
  {"Clock divide", {0xD5, 0x80}, 2},
  {"Multiplex 64", {0xA8, 0x3F}, 2},
  {"Display offset 0", {0xD3, 0x00}, 2},
  {"Start line 0", {0x40, 0x00}, 1},
  {"Horizontal addressing", {0x20, 0x00}, 2},
  {"Segment remap", {0xA1, 0x00}, 1},
  {"COM scan descending", {0xC8, 0x00}, 1},
  {"COM pins 128x64", {0xDA, 0x12}, 2},
  {"Contrast", {0x81, 0xCF}, 2},
  {"Pre-charge", {0xD9, 0xF1}, 2},
  {"VCOMH", {0xDB, 0x40}, 2},
  {"Charge pump ON", {0x8D, 0x14}, 2},
  {"Show RAM content", {0xA4, 0x00}, 1},
  {"Normal colors", {0xA6, 0x00}, 1},
  {"Scroll OFF", {0x2E, 0x00}, 1},
  {"Display ON", {0xAF, 0x00}, 1},
};

bool displayReady = false;  // true only when init and drawing succeeded
bool inverted = false;      // current color mode
uint32_t lastInvertMs = 0;  // when the mode was last switched

// One I2C transaction: START, address, control byte, payload, STOP
// Returns 0 on ACK, any other value is an error code from Wire
uint8_t sendBytes(ControlByte control, const uint8_t* bytes, size_t length) {
  Wire.beginTransmission(Config::oledAddress);
  Wire.write(static_cast<uint8_t>(control));
  Wire.write(bytes, length);
  return Wire.endTransmission();
}

// True if a device answers ACK at this address
bool isPresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

// I2C scanner: knocks on every 7-bit address and prints those that answer
void scanBus() {
  uint8_t found = 0;
  Serial.println("I2C scan 0x08..0x77:");
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    if (isPresent(address)) {
      Serial.printf("  found device at 0x%02X\n", address);
      ++found;
    }
  }
  Serial.printf("Total found: %u\n", found);
}

// Sends the init plan step by step and logs the result of each step
bool runInitPlan() {
  Serial.println("Init plan:");
  for (const InitStep& step : initPlan) {
    const uint8_t result = sendBytes(ControlByte::Command, step.bytes, step.length);
    Serial.printf("  %-22s 0x%02X", step.name, step.bytes[0]);
    if (step.length == 2) {
      Serial.printf(" 0x%02X", step.bytes[1]);
    } else {
      Serial.print("     ");
    }
    Serial.printf(" -> %s (%u)\n", result == 0 ? "ACK" : "ERROR", result);
    if (result != 0) {
      return false;
    }
  }
  return true;
}

// One byte = 8 vertical pixels; squares are 8x8 pixels
uint8_t checkerByte(uint8_t page, uint8_t column) {
  return ((column / 8 + page) % 2 == 0) ? 0xFF : 0x00;
}

// Sets the drawing window to the whole screen and fills it with a checkerboard
bool drawCheckerboard() {
  constexpr uint8_t window[] = {
    0x21, 0, Config::width - 1,  // column address: 0..127
    0x22, 0, Config::pages - 1   // page address: 0..7
  };
  if (sendBytes(ControlByte::Command, window, sizeof(window)) != 0) {
    return false;
  }
  uint8_t chunk[Config::chunkSize];
  for (uint8_t page = 0; page < Config::pages; ++page) {
    for (uint8_t column = 0; column < Config::width; column += Config::chunkSize) {
      for (uint8_t i = 0; i < Config::chunkSize; ++i) {
        chunk[i] = checkerByte(page, column + i);
      }
      if (sendBytes(ControlByte::Data, chunk, sizeof(chunk)) != 0) {
        return false;
      }
    }
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && millis() - waitStart < Config::serialWaitMs) {
    // wait for the serial monitor so the scan result is not lost
  }

  Wire.begin(Config::sdaPin, Config::sclPin, Config::i2cHz);
  Serial.println();
  Serial.println("=== Module 4.2: SSD1306 over I2C ===");
  scanBus();

  if (!isPresent(Config::oledAddress)) {
    Serial.printf("OLED not found at 0x%02X: check GND, SDA/SCL, power, pull-ups\n",
                  Config::oledAddress);
    return;
  }
  if (!runInitPlan()) {
    Serial.println("Init failed");
    return;
  }
  if (!drawCheckerboard()) {
    Serial.println("Drawing failed");
    return;
  }
  Serial.println("Checkerboard drawn, display ready");
  displayReady = true;
  lastInvertMs = millis();
}

void loop() {
  if (!displayReady) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastInvertMs < Config::invertPeriodMs) {
    return;
  }
  lastInvertMs = now;
  inverted = !inverted;
  const uint8_t command = inverted ? 0xA7 : 0xA6;
  const uint8_t result = sendBytes(ControlByte::Command, &command, 1);
  Serial.printf("0x%02X %s -> %s\n", command, inverted ? "Inverse" : "Normal",
                result == 0 ? "ACK" : "ERROR");
}
