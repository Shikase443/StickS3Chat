# StickS3 Chat

Voice chat firmware for the M5Stack StickS3 (ESP32-S3). It records speech with push-to-talk, sends it to configurable AI services, and plays the generated response through the built-in speaker.

## Features

- Push-to-talk recording from the home screen
- Explicit PSRAM recording buffer with a 30-second limit
- Separate STT, LLM, and TTS APIs
- Integrated voice-agent API mode
- OpenAI-compatible, Gemini, and Anthropic providers where applicable
- OpenAI Responses and Chat Completions URL detection
- Optional OpenClaw and Hermes Agent session headers
- Temporary password-protected WebUI
- Wi-Fi, service, NTP, time-zone, volume, and brightness settings stored in NVS
- NTP clock, Wi-Fi status, battery status, and lightweight animated face

## Requirements

- M5Stack StickS3
- ESP-IDF 5.4 or later
- USB connection for flashing

Dependencies are resolved through the ESP-IDF Component Manager. M5Unified is declared in `main/idf_component.yml`.

## Build and flash

Activate ESP-IDF, then run:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM8 flash
```

Replace `COM8` with the port assigned to your device.

## Device controls

- Home, no menu selected: hold A to record; release A to send
- Home: press B to select `Config`; press A to open it
- Config: press B to move the selection; press A to activate it
- Text input: tilt left or right to select characters; tilt forward for `OK`; tilt backward for `DEL`; press A to activate the selected item

## Initial setup

1. Open `Config` on the device.
2. Enter the Wi-Fi SSID and password.
3. After Wi-Fi connects, open the displayed WebUI URL.
4. Sign in with the temporary five-digit password shown on the device.
5. Configure either `Separate APIs` or `Integrated API` and save.

The WebUI server is available only while the Config screen is open. A new temporary password is generated whenever the server starts.

## Configuration storage

Wi-Fi credentials, API URLs, API keys, session identifiers, and UI settings are entered at runtime and stored in the device's NVS. This repository contains no preconfigured Wi-Fi credentials, API keys, private server addresses, or session identifiers.

Fresh installations default to `pool.ntp.org` and UTC. Both values can be changed from the WebUI.
