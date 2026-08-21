// HC-12 receiver and quiet buzzer -- BEDROOM UNIT.
// Sounds continuously after receiving "RING" until either unit sends "STOP".
//
// HC-12 pin -> ESP32 DevKit pin
//   VCC       -> 5V / VIN
//   GND       -> GND
//   TXD       -> GPIO16 / RX2
//   RXD       -> GPIO17 / TX2
//   SET       -> 3V3
//
// Buzzer pin -> ESP32 DevKit pin
//   VCC        -> 3V3
//   GND        -> GND
//   I/O        -> GPIO26
//
// Button pin 1 -> ESP32 GPIO25
// Button pin 2 -> GND
//
// Blue LED -> ESP32 GPIO2 (active HIGH)

constexpr uint8_t BUZZER_PIN = 26;
constexpr uint8_t BUTTON_PIN = 25;
constexpr uint8_t HC12_RX = 16;  // Connect to HC-12 TXD.
constexpr uint8_t HC12_TX = 17;  // Connect to HC-12 RXD.
constexpr uint8_t BLUE_LED_PIN = 2;
// A lower, wider pulse is noticeably louder without the sharp 800 Hz tone.
constexpr uint16_t TONE_FREQUENCY_HZ = 350;
constexpr uint8_t BUZZER_DUTY_PERCENT = 8;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BEEP_DURATION_MS = 250;
constexpr uint32_t BEEP_START_INTERVAL_MS = 1000;
constexpr uint32_t PATTERN_REPEAT_PAUSE_MS = 3000;

HardwareSerial HC12(2);

bool buttonRawState = HIGH;
bool buttonStableState = HIGH;
uint32_t buttonRawChangedAtMs = 0;
bool ringing = false;
bool acknowledgementArmed = false;
bool buzzerToneActive = false;
uint8_t completedBeeps = 0;
uint32_t buzzerTransitionAtMs = 0;

bool buttonWasPressed();
void receiveMessages();
void playQuietToneCycle(uint16_t frequencyHz, uint8_t dutyPercent);
void updateBuzzerPattern();
void setRinging(bool active);

void setup() {
  Serial.begin(115200);
  HC12.begin(9600, SERIAL_8N1, HC12_RX, HC12_TX);

  // This module is low-triggered. Set the output latch before enabling the
  // output to avoid an unwanted chirp during startup.
  digitalWrite(BUZZER_PIN, HIGH);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(BLUE_LED_PIN, LOW);
  pinMode(BLUE_LED_PIN, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  buttonRawState = digitalRead(BUTTON_PIN);
  buttonStableState = buttonRawState;
  buttonRawChangedAtMs = millis();

  delay(500);
  Serial.println("Bedroom HC-12 receiver ready; waiting for RING.");
}

void loop() {
  const bool buttonPressed = buttonWasPressed();

  if (ringing) {
    // A button held when RING arrived must be released before a later press
    // can acknowledge the ring.
    if (buttonRawState == HIGH && buttonStableState == HIGH) {
      acknowledgementArmed = true;
    }

    if (buttonPressed && acknowledgementArmed) {
      setRinging(false);
      acknowledgementArmed = false;
      HC12.println("STOP");
      Serial.println("Ring acknowledged.");
    }
  }

  receiveMessages();

  if (ringing) {
    updateBuzzerPattern();
  }
}

bool buttonWasPressed() {
  const bool currentRawState = digitalRead(BUTTON_PIN);
  const uint32_t nowMs = millis();

  if (currentRawState != buttonRawState) {
    buttonRawState = currentRawState;
    buttonRawChangedAtMs = nowMs;
  }

  if (buttonStableState != buttonRawState &&
      nowMs - buttonRawChangedAtMs >= BUTTON_DEBOUNCE_MS) {
    buttonStableState = buttonRawState;
    return buttonStableState == LOW;
  }

  return false;
}

void receiveMessages() {
  static char message[16];
  static size_t messageLength = 0;

  while (HC12.available() > 0) {
    const char received = static_cast<char>(HC12.read());

    if (received == '\n') {
      message[messageLength] = '\0';

      if (strcmp(message, "RING") == 0) {
        if (!ringing) {
          setRinging(true);
          acknowledgementArmed =
              buttonRawState == HIGH && buttonStableState == HIGH;
          Serial.println("Received: RING; sounding until acknowledged.");
        }
      } else if (strcmp(message, "STOP") == 0) {
        if (ringing) {
          setRinging(false);
          acknowledgementArmed = false;
          Serial.println("Received: STOP; ring stopped.");
        }
      } else if (messageLength > 0) {
        Serial.print("Ignored: ");
        Serial.println(message);
      }

      messageLength = 0;
    } else if (received != '\r') {
      if (messageLength < sizeof(message) - 1) {
        message[messageLength++] = received;
      } else {
        // Discard an overlong or corrupt message.
        messageLength = 0;
      }
    }
  }
}

void setRinging(bool active) {
  ringing = active;
  digitalWrite(BLUE_LED_PIN, active ? HIGH : LOW);

  if (active) {
    // Start the first of three beeps immediately.
    buzzerToneActive = true;
    completedBeeps = 0;
    buzzerTransitionAtMs = millis() + BEEP_DURATION_MS;
  } else {
    buzzerToneActive = false;
    digitalWrite(BUZZER_PIN, HIGH);
  }
}

void updateBuzzerPattern() {
  const uint32_t nowMs = millis();

  if (buzzerToneActive) {
    // One short cycle per pass keeps radio and button handling responsive.
    playQuietToneCycle(TONE_FREQUENCY_HZ, BUZZER_DUTY_PERCENT);

    if (static_cast<int32_t>(nowMs - buzzerTransitionAtMs) >= 0) {
      buzzerToneActive = false;
      digitalWrite(BUZZER_PIN, HIGH);
      ++completedBeeps;
      buzzerTransitionAtMs = nowMs +
          (completedBeeps == 3 ? PATTERN_REPEAT_PAUSE_MS
                               : BEEP_START_INTERVAL_MS - BEEP_DURATION_MS);
    }
  } else if (static_cast<int32_t>(nowMs - buzzerTransitionAtMs) >= 0) {
    if (completedBeeps == 3) {
      completedBeeps = 0;
    }
    buzzerToneActive = true;
    buzzerTransitionAtMs = nowMs + BEEP_DURATION_MS;
  }
}

void playQuietToneCycle(uint16_t frequencyHz, uint8_t dutyPercent) {
  const uint32_t periodUs = 1000000UL / frequencyHz;
  const uint32_t lowTimeUs = (periodUs * dutyPercent) / 100UL;
  const uint32_t highTimeUs = periodUs - lowTimeUs;

  // LOW activates this module. One short cycle is generated per pass through
  // loop() so the radio and acknowledgement button remain responsive.
  digitalWrite(BUZZER_PIN, LOW);
  delayMicroseconds(lowTimeUs);
  digitalWrite(BUZZER_PIN, HIGH);
  delayMicroseconds(highTimeUs);
}
