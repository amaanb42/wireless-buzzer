// HC-12 kitchen unit
// Sends "RING" on the first button press and "STOP" on the next press.
//
// Wiring:
//   HC-12 RXD -> ESP32 GPIO 17 (TX2)
//   HC-12 TXD -> ESP32 GPIO 16 (RX2)
//   HC-12 SET -> 3.3V
//   HC-12 VCC -> 5V pin
//   HC-12 GND -> GND
//   Button pin 1 -> ESP32 GPIO 25
//   Button pin 2 -> GND
//   Blue LED     -> ESP32 GPIO 2 (active HIGH)

constexpr uint8_t HC12_RX = 16;  // Connect to HC-12 TXD.
constexpr uint8_t HC12_TX = 17;  // Connect to HC-12 RXD.
constexpr uint8_t BUTTON_PIN = 25;
constexpr uint8_t BLUE_LED_PIN = 2;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

HardwareSerial HC12(2);  // use UART2

bool buttonRawState = HIGH;
bool buttonStableState = HIGH;
uint32_t buttonRawChangedAtMs = 0;
bool ringActive = false;

bool buttonWasPressed();
void receiveMessages();
void setRingActive(bool active);

void setup() {
  Serial.begin(115200);
  HC12.begin(9600, SERIAL_8N1, HC12_RX, HC12_TX);  // HC-12 default baud is 9600

  digitalWrite(BLUE_LED_PIN, LOW);
  pinMode(BLUE_LED_PIN, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  buttonRawState = digitalRead(BUTTON_PIN);
  buttonStableState = buttonRawState;
  buttonRawChangedAtMs = millis();

  Serial.println("Kitchen transmitter ready; press the button to ring.");
}

void loop() {
  if (buttonWasPressed()) {
    if (ringActive) {
      HC12.println("STOP");
      setRingActive(false);
      Serial.println("Sent: STOP");
    } else {
      HC12.println("RING");
      setRingActive(true);
      Serial.println("Sent: RING");
    }
  }

  receiveMessages();
}

void receiveMessages() {
  static char message[16];
  static size_t messageLength = 0;

  while (HC12.available() > 0) {
    const char received = static_cast<char>(HC12.read());

    if (received == '\n') {
      message[messageLength] = '\0';

      if (strcmp(message, "RING") == 0) {
        setRingActive(true);
        Serial.println("Received: RING");
      } else if (strcmp(message, "STOP") == 0) {
        setRingActive(false);
        Serial.println("Received: STOP");
      } else if (messageLength > 0) {
        Serial.print("Ignored: ");
        Serial.println(message);
      }

      messageLength = 0;
    } else if (received != '\r') {
      if (messageLength < sizeof(message) - 1) {
        message[messageLength++] = received;
      } else {
        messageLength = 0;
      }
    }
  }
}

void setRingActive(bool active) {
  ringActive = active;
  digitalWrite(BLUE_LED_PIN, active ? HIGH : LOW);
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
