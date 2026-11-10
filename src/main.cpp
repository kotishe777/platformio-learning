/*
 * Module 3.4 hometask (task 1) — non-blocking buzzer player via LEDC PWM
 * Board: ESP32-S3 DevKitC-1 N16R8, framework Arduino (Core 3.x)
 * Buzzer: GPIO17 -> 220R -> passive buzzer + , buzzer - -> GND
 * LED:    GPIO15 -> 220R -> LED -> GND (blinks every 250 ms while music plays)
 * Button: GPIO6 -> button -> GND (INPUT_PULLUP), press = pause / resume
 * Fixed 50 ms tick from millis(), no delay(), no FreeRTOS tasks
 * Melody: Twinkle Twinkle Little Star, durations in ticks
 */
#include <Arduino.h>

struct Config {
  static constexpr uint8_t  buzzerPin     = 17;
  static constexpr uint8_t  ledPin        = 15;
  static constexpr uint8_t  buttonPin     = 6;
  static constexpr uint32_t pwmFreqHz     = 1000;
  static constexpr uint8_t  pwmResolution = 10;
  static constexpr uint32_t tickMs        = 50;
  static constexpr uint32_t blinkMs       = 250;
  static constexpr uint8_t  gapTicks      = 1;
};

enum class Note : uint16_t {
  Rest = 0,
  C4   = 262,
  D4   = 294,
  E4   = 330,
  F4   = 349,
  G4   = 392,
  A4   = 440,
  B4   = 494,
  C5   = 523,
};

struct Step {
  Note    note;
  uint8_t ticks;
};

constexpr Step melody[] = {
  {Note::C4, 8}, {Note::C4, 8}, {Note::G4, 8}, {Note::G4, 8},
  {Note::A4, 8}, {Note::A4, 8}, {Note::G4, 16},
  {Note::F4, 8}, {Note::F4, 8}, {Note::E4, 8}, {Note::E4, 8},
  {Note::D4, 8}, {Note::D4, 8}, {Note::C4, 16},
  {Note::Rest, 40},
};
constexpr size_t melodyLength = sizeof(melody) / sizeof(melody[0]);

constexpr bool melodyIsValid() {
  for (size_t i = 0; i < melodyLength; ++i) {
    if (melody[i].ticks <= Config::gapTicks) {
      return false;
    }
  }
  return true;
}
static_assert(melodyLength > 0, "Melody must not be empty");
static_assert(melodyIsValid(), "Every step must be longer than the gap");

const char* noteName(Note note) {
  switch (note) {
    case Note::C4: return "C4";
    case Note::D4: return "D4";
    case Note::E4: return "E4";
    case Note::F4: return "F4";
    case Note::G4: return "G4";
    case Note::A4: return "A4";
    case Note::B4: return "B4";
    case Note::C5: return "C5";
    default:       return "--";
  }
}

class MelodyPlayer {
 public:
  bool begin() {
    const bool ok = ledcAttach(Config::buzzerPin, Config::pwmFreqHz, Config::pwmResolution);
    silence();
    return ok;
  }

  void play() {
    index_   = 0;
    playing_ = true;
    startStep();
  }

  void toggle() {
    if (playing_) {
      playing_ = false;
      silence();
    } else {
      playing_ = true;
      startStep();
    }
  }

  bool playing() const {
    return playing_;
  }

  void tick() {
    if (!playing_) {
      return;
    }
    --ticksLeft_;
    if (ticksLeft_ == 0) {
      index_ = (index_ + 1) % melodyLength;
      startStep();
    } else if (ticksLeft_ == Config::gapTicks) {
      silence();
    }
  }

 private:
  void startStep() {
    const Step& step = melody[index_];
    ticksLeft_ = step.ticks;
    if (step.note == Note::Rest) {
      silence();
    } else {
      ledcWriteTone(Config::buzzerPin, static_cast<uint32_t>(step.note));
    }
    Serial.printf("Step %2u: %s %3u Hz, %2u ticks\n",
                  static_cast<unsigned>(index_), noteName(step.note),
                  static_cast<unsigned>(step.note), static_cast<unsigned>(step.ticks));
  }

  void silence() {
    ledcWrite(Config::buzzerPin, 0);
  }

  size_t  index_     = 0;
  uint8_t ticksLeft_ = 0;
  bool    playing_   = false;
};

MelodyPlayer player;
uint32_t     lastTickMs     = 0;
uint32_t     lastBlinkMs    = 0;
bool         ledOn          = false;
bool         lastButtonDown = false;

void setup() {
  Serial.begin(115200);
  pinMode(Config::ledPin, OUTPUT);
  pinMode(Config::buttonPin, INPUT_PULLUP);

  if (!player.begin()) {
    Serial.println("LEDC attach failed");
  }
  player.play();

  lastTickMs  = millis();
  lastBlinkMs = millis();
}

void loop() {
  const uint32_t now = millis();

  if (now - lastTickMs >= Config::tickMs) {
    lastTickMs += Config::tickMs;

    const bool down = digitalRead(Config::buttonPin) == LOW;
    if (down && !lastButtonDown) {
      player.toggle();
      Serial.println(player.playing() ? "Resume" : "Pause");
    }
    lastButtonDown = down;

    player.tick();
  }

  if (now - lastBlinkMs >= Config::blinkMs) {
    lastBlinkMs += Config::blinkMs;
    ledOn = !ledOn;
    digitalWrite(Config::ledPin, ledOn ? HIGH : LOW);
  }
}