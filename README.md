AI DISCLAIMER: Every file in this project has been written mostly by codex

# Wireless Buzzer

Wireless Buzzer is a two-unit wireless buzzer system built with ESP32 boards and
HC-12 serial radio modules. One unit lives in the kitchen and the other in the
bedroom.

## Current behavior

- Pressing the kitchen button sends `RING` to the bedroom unit.
- The bedroom unit sounds its buzzer continuously while the ring is active.
- A second kitchen-button press sends `STOP` and stops the bedroom buzzer.
- Pressing the bedroom button also stops the buzzer and sends `STOP` back to the
  kitchen unit.
- Both blue LEDs indicate the active ring state and turn off when either unit
  stops the ring.
- Buttons use a 30 ms debounce interval.

The radio messages are newline-terminated text commands: `RING` and `STOP`.

## Hardware and wiring

Both units use an ESP32 DevKit-style board and an HC-12 module.

### HC-12 on both units

| HC-12 pin | ESP32 connection |
| --- | --- |
| TXD | GPIO 16 / UART2 RX |
| RXD | GPIO 17 / UART2 TX |
| SET | 3.3V |
| VCC | 5V / VIN |
| GND | GND |

### Shared controls

- Push button: GPIO 25 to GND, using the ESP32 internal pull-up.
- Blue LED: GPIO 2, active HIGH.

### Bedroom-only buzzer

- Buzzer I/O: GPIO 26.
- Buzzer VCC: 3.3V.
- Buzzer GND: GND.

The buzzer frequency and duty cycle are configured near the top of
`bedroom_hc12_test/bedroom_hc12_test.ino` with `TONE_FREQUENCY_HZ` and
`BUZZER_DUTY_PERCENT`.

## Sketches

- `kitchen_hc12_test/kitchen_hc12_test.ino` — kitchen sender and cancellation
  controller.
- `bedroom_hc12_test/bedroom_hc12_test.ino` — bedroom receiver, buzzer, LED,
  and local cancellation controller.

## Compile and upload

With the kitchen unit on `/dev/ttyUSB0` and the bedroom unit on
`/dev/ttyUSB1`, run these commands from the project root:

```bash
arduino-cli compile --upload \
  -p /dev/ttyUSB0 \
  --fqbn esp32:esp32:esp32 \
  kitchen_hc12_test

arduino-cli compile --upload \
  -p /dev/ttyUSB1 \
  --fqbn esp32:esp32:esp32 \
  bedroom_hc12_test
```

Check the connected ports with:

```bash
arduino-cli board list
```

## Planned mobile-app support

The next major feature is allowing a phone to trigger the buzzer. The bedroom ESP32
will act as a small web server, exposing a simple local web interface or HTTP
endpoint for a mobile device on the same Wi-Fi network. When the endpoint is
triggered, that ESP32 will send the existing `RING` command over the HC-12 link,
so the bedroom unit can reuse the current buzzer, LED, and cancellation logic.

The planned design should preserve physical-button control and add:

1. Wi-Fi setup and a lightweight HTTP server on the chosen ESP32.
2. A mobile-friendly page with a ring/stop control.
3. Authentication or local-network safeguards before exposing the endpoint.
4. Clear handling for Wi-Fi loss so HC-12-based physical ringing remains
   available.

The web API is intentionally not implemented yet; the current radio protocol
remains the simple `RING`/`STOP` interface.
