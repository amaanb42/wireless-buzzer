// HC-12 receiver, Wi-Fi server, and buzzer -- BEDROOM UNIT.
// Sounds a repeating beep pattern after receiving "RING" until stopped.
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

#include <WebServer.h>
#include <WiFi.h>

#include "wifi_secrets.h"

constexpr uint8_t BUZZER_PIN = 26;
constexpr uint8_t BUTTON_PIN = 25;
constexpr uint8_t HC12_RX = 16;  // Connect to HC-12 TXD.
constexpr uint8_t HC12_TX = 17;  // Connect to HC-12 RXD.
constexpr uint8_t BLUE_LED_PIN = 2;
// A lower, wider pulse is noticeably louder without the sharp 800 Hz tone.
constexpr uint16_t TONE_FREQUENCY_HZ = 350;
constexpr uint8_t BUZZER_DUTY_PERCENT = 8;
constexpr uint8_t BUZZER_PWM_RESOLUTION_BITS = 10;
constexpr uint16_t BUZZER_PWM_FULL_SCALE =
    1U << BUZZER_PWM_RESOLUTION_BITS;
constexpr uint16_t BUZZER_PWM_ON_DUTY =
    (BUZZER_PWM_FULL_SCALE * (100U - BUZZER_DUTY_PERCENT)) / 100U;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BEEP_DURATION_MS = 250;
constexpr uint32_t BEEP_START_INTERVAL_MS = 1000;
constexpr uint32_t PATTERN_REPEAT_PAUSE_MS = 3000;
constexpr uint32_t RING_TIMEOUT_MS = 150000;

HardwareSerial HC12(2);
WebServer webServer(80);

bool buttonRawState = HIGH;
bool buttonStableState = HIGH;
uint32_t buttonRawChangedAtMs = 0;
bool ringing = false;
bool acknowledgementArmed = false;
bool buzzerToneActive = false;
uint8_t completedBeeps = 0;
uint32_t buzzerTransitionAtMs = 0;
uint32_t ringStartedAtMs = 0;
bool webServerRunning = false;

enum class ChangeSource : uint8_t {
  STARTUP,
  API,
  BEDROOM_BUTTON,
  HC12,
  TIMEOUT,
};

ChangeSource lastChangeSource = ChangeSource::STARTUP;

bool buttonWasPressed();
void receiveMessages();
void setBuzzerTone(bool active);
void updateBuzzerPattern();
void updateRingTimeout();
void configureWebServer();
void updateWiFi();
void sendRingingStatus();
const char* changeSourceName(ChangeSource source);
void setRinging(bool active, ChangeSource source);

void setup() {
  HC12.begin(9600, SERIAL_8N1, HC12_RX, HC12_TX);

  // This module is low-triggered. Set the output latch before enabling the
  // output to avoid an unwanted chirp during startup.
  digitalWrite(BUZZER_PIN, HIGH);
  pinMode(BUZZER_PIN, OUTPUT);
  ledcAttach(BUZZER_PIN, TONE_FREQUENCY_HZ,
             BUZZER_PWM_RESOLUTION_BITS);
  setBuzzerTone(false);

  digitalWrite(BLUE_LED_PIN, LOW);
  pinMode(BLUE_LED_PIN, OUTPUT);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  buttonRawState = digitalRead(BUTTON_PIN);
  buttonStableState = buttonRawState;
  buttonRawChangedAtMs = millis();

  configureWebServer();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  updateWiFi();

  const bool buttonPressed = buttonWasPressed();

  if (ringing) {
    // A button held when RING arrived must be released before a later press
    // can acknowledge the ring.
    if (buttonRawState == HIGH && buttonStableState == HIGH) {
      acknowledgementArmed = true;
    }

    if (buttonPressed && acknowledgementArmed) {
      setRinging(false, ChangeSource::BEDROOM_BUTTON);
      acknowledgementArmed = false;
      HC12.println("STOP");
    }
  }

  receiveMessages();
  updateRingTimeout();

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
          setRinging(true, ChangeSource::HC12);
          acknowledgementArmed =
              buttonRawState == HIGH && buttonStableState == HIGH;
        }
      } else if (strcmp(message, "STOP") == 0) {
        if (ringing) {
          setRinging(false, ChangeSource::HC12);
          acknowledgementArmed = false;
        }
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

void setRinging(bool active, ChangeSource source) {
  if (ringing == active) {
    return;
  }

  ringing = active;
  lastChangeSource = source;
  digitalWrite(BLUE_LED_PIN, active ? HIGH : LOW);

  if (active) {
    ringStartedAtMs = millis();
    // Start the first of three beeps immediately.
    buzzerToneActive = true;
    setBuzzerTone(true);
    completedBeeps = 0;
    buzzerTransitionAtMs = millis() + BEEP_DURATION_MS;
  } else {
    buzzerToneActive = false;
    setBuzzerTone(false);
  }
}

void configureWebServer() {
  webServer.on("/api/status", HTTP_GET, []() {
    sendRingingStatus();
  });

  webServer.on("/api/ring", HTTP_POST, []() {
    if (!ringing) {
      setRinging(true, ChangeSource::API);
      acknowledgementArmed =
          buttonRawState == HIGH && buttonStableState == HIGH;
    }
    HC12.println("RING");
    sendRingingStatus();
  });

  webServer.on("/api/stop", HTTP_POST, []() {
    setRinging(false, ChangeSource::API);
    acknowledgementArmed = false;
    HC12.println("STOP");
    sendRingingStatus();
  });

  webServer.onNotFound([]() {
    webServer.send(404, "application/json", "{\"error\":\"not_found\"}");
  });
}

void updateWiFi() {
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (connected) {
    if (!webServerRunning) {
      webServer.begin();
      webServerRunning = true;
    }

    webServer.handleClient();
    return;
  }

  if (webServerRunning) {
    webServer.stop();
    webServerRunning = false;
  }
}

void sendRingingStatus() {
  char responseBody[80];
  snprintf(responseBody, sizeof(responseBody),
           "{\"ringing\":%s,\"source\":\"%s\"}",
           ringing ? "true" : "false", changeSourceName(lastChangeSource));
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", responseBody);
}

const char* changeSourceName(ChangeSource source) {
  switch (source) {
    case ChangeSource::API:
      return "api";
    case ChangeSource::BEDROOM_BUTTON:
      return "bedroom_button";
    case ChangeSource::HC12:
      return "hc12";
    case ChangeSource::TIMEOUT:
      return "timeout";
    case ChangeSource::STARTUP:
    default:
      return "startup";
  }
}

void updateBuzzerPattern() {
  const uint32_t nowMs = millis();

  if (buzzerToneActive) {
    if (static_cast<int32_t>(nowMs - buzzerTransitionAtMs) >= 0) {
      buzzerToneActive = false;
      setBuzzerTone(false);
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
    setBuzzerTone(true);
    buzzerTransitionAtMs = nowMs + BEEP_DURATION_MS;
  }
}

void updateRingTimeout() {
  if (!ringing || millis() - ringStartedAtMs < RING_TIMEOUT_MS) {
    return;
  }

  setRinging(false, ChangeSource::TIMEOUT);
  acknowledgementArmed = false;
  HC12.println("STOP");
}

void setBuzzerTone(bool active) {
  // This buzzer is active LOW, so an 8% activation duty is 92% HIGH.
  ledcWrite(BUZZER_PIN,
            active ? BUZZER_PWM_ON_DUTY : BUZZER_PWM_FULL_SCALE);
}
