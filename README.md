AI DISCLAIMER: Every file in this project has been written mostly by codex

# Wireless Buzzer

Wireless Buzzer is a two-unit wireless buzzer system built with ESP32 boards and
HC-12 serial radio modules. One unit lives in the kitchen and the other in the
bedroom.

## Current behavior

- Pressing the kitchen button sends `RING` to the bedroom unit.
- The bedroom unit repeats three short beeps followed by a three-second pause
  while the ring is active.
- A second kitchen-button press sends `STOP` and stops the bedroom buzzer.
- Pressing the bedroom button also stops the buzzer and sends `STOP` back to the
  kitchen unit.
- Both blue LEDs indicate the active ring state and turn off when either unit
  stops the ring.
- The bedroom unit exposes a local Wi-Fi API for ringing, stopping, and reading
  the current state. Web commands synchronize the kitchen LED over HC-12.
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
`bedroom_hc12/bedroom_hc12.ino` with `TONE_FREQUENCY_HZ` and
`BUZZER_DUTY_PERCENT`.

## Sketches

- `kitchen_hc12/kitchen_hc12.ino` — kitchen sender and cancellation
  controller.
- `bedroom_hc12/bedroom_hc12.ino` — bedroom receiver, buzzer, LED, local
  cancellation controller, and Wi-Fi server.

## Wi-Fi setup

Create the ignored local credential file before compiling the bedroom sketch:

```bash
cp bedroom_hc12/wifi_secrets.example.h bedroom_hc12/wifi_secrets.h
```

Edit `bedroom_hc12/wifi_secrets.h` and replace `YOUR_WIFI_SSID` and
`YOUR_WIFI_PASSWORD`. The real file is ignored by Git. Wi-Fi connection and
reconnection are non-blocking, so HC-12 and physical-button control continue to
work if Wi-Fi is unavailable.

Reserve the bedroom ESP32's Wi-Fi MAC address in the router so the API keeps a
stable IP. This installation uses `192.168.50.50`. The production sketch does
not emit serial diagnostics or advertise an mDNS hostname.

## HTTP API

The server listens on port 80 and returns JSON. It is intentionally available
without authentication to devices on the same trusted Wi-Fi network.

| Method | Path | Behavior |
| --- | --- | --- |
| `GET` | `/api/status` | Returns the current `ringing` state. |
| `POST` | `/api/ring` | Starts ringing and turns on both units' LEDs. |
| `POST` | `/api/stop` | Stops ringing and turns off both units' LEDs. |

Example requests:

```bash
curl http://192.168.50.50/api/status
curl -X POST http://192.168.50.50/api/ring
curl -X POST http://192.168.50.50/api/stop
```

Each successful response is either `{"ringing":true}` or
`{"ringing":false}`. A future Android app can poll `/api/status` about every
500 ms to notice kitchen, bedroom-button, and web-request state changes.

## Compile and upload

Check the connected ports, then run these commands from the project root with
the appropriate port for each unit. For example, with the bedroom currently on
`/dev/ttyUSB0`:

```bash
arduino-cli compile --upload \
  -p /dev/ttyUSB_KITCHEN \
  --fqbn esp32:esp32:esp32 \
  kitchen_hc12

arduino-cli compile --upload \
  -p /dev/ttyUSB_BEDROOM \
  --fqbn esp32:esp32:esp32 \
  bedroom_hc12
```

Check the connected ports with:

```bash
arduino-cli board list
```
