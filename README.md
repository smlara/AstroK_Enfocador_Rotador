# AstroK — DIY Focuser & Rotator Controller for Astrophotography

[![License: GPL v3](https://img.shields.io/badge/Firmware-GPL%20v3-blue.svg)](LICENSE)
[![License: CERN OHL v2-S](https://img.shields.io/badge/Hardware-CERN%20OHL%20v2--S-orange.svg)](LICENSE-HARDWARE)
[![Platform: ESP32-C3](https://img.shields.io/badge/Platform-ESP32--C3-teal.svg)](https://www.espressif.com/)
[![Protocol: ASCOM](https://img.shields.io/badge/Protocol-ASCOM%20%7C%20ALPACA%20%7C%20Moonlite-lightgrey.svg)]()

> **[Versión en Español](README_ES.md)**

A fully open-source DIY controller for a telescope **focuser** or a camera **rotator**, based on an **ESP32-C3** and a **TMC2209** stepper driver. The same board and firmware work as either device, selected by a jumper. It integrates with capture software such as N.I.N.A., Sequence Generator Pro and others through its own **ASCOM driver** over USB, or through **ASCOM ALPACA** over WiFi.

---

## Features

- **Focuser or rotator** — one board, one firmware; the mode is set by a jumper
- **ASCOM driver for Windows** (USB) and **ASCOM ALPACA** (WiFi, with discovery)
- **Moonlite-compatible serial protocol** over USB
- **Manual control** via rotary encoder (x10 / x100 / x500 steps, or x0.1° / x1° in rotator mode)
- **OLED display** (128×32) showing position and active step
- **Web configuration page** served by the device itself (English / Spanish)
- **Firmware updates over USB** from the configuration app, including the web page
- **Silent operation** (StealthChop) and optional sensorless stall detection
- **Firmware version 1.4.3** · **ASCOM app version 2.0.1**

---

## Hardware

| Component | Description |
|-----------|-------------|
| ESP32-C3 SuperMini | Microcontroller with native USB and WiFi |
| TMC2209 module | Stepper driver, configured over UART |
| Stepper motor | NEMA 17 (17HS4023), or 35BY48 geared motor (120:1) for focusers |
| OLED SSD1306 | 128×32 px I2C display |
| Rotary encoder | EC11 or compatible, with push button |
| Power supply | 12 V for the motor |

The PCB design and bill of materials are in [pcb/](pcb/).

---

## Bill of Materials (BOM)

| Qty | Component | Specs |
|:---:|-----------|-------|
| 1 | ESP32-C3 SuperMini | USB-C |
| 1 | TMC2209 module | UART mode |
| 1 | Stepper motor | 17HS4023 or 35BY48 (120:1) |
| 1 | OLED display | SSD1306, 128×32 px, I2C, 3.3 V |
| 1 | Rotary encoder | EC11 or compatible, with push button |
| 1 | Jumper | Focuser / rotator mode selection |
| 1 | DC jack | DC-005, 12 V input |
| 1 | Voltage regulator | 7805, 12 V → 5 V for the logic |
| — | PCB | See [pcb/](pcb/) for the full BOM |

---

## Pin Assignment (ESP32-C3 SuperMini)

| GPIO | Signal          | Component         |
|:----:|-----------------|-------------------|
| 5    | STEP            | TMC2209           |
| 6    | DIR             | TMC2209           |
| 7    | EN              | TMC2209           |
| 21   | UART TX         | TMC2209           |
| 20   | UART RX         | TMC2209           |
| 10   | DIAG            | TMC2209 (stall)   |
| 1    | CLK             | Rotary encoder    |
| 3    | DT              | Rotary encoder    |
| 0    | SW (button)     | Rotary encoder    |
| 4    | Mode selector   | Jumper (LOW = rotator) |
| 8    | SDA             | OLED (I2C)        |
| 9    | SCL             | OLED (I2C)        |

---

## Communication

| Interface | Details |
|-----------|---------|
| USB serial | **115200 bps**, Moonlite-compatible commands (`:GP#`, `:SN…#`, `:FG#`, `:FQ#`…) |
| ASCOM ALPACA | HTTP on port **11111**, UDP discovery on port **32227** |
| Web setup | `http://<device-ip>/` |

Positions are reported in hex with at least 4 digits, so focusers can go up to 999999 steps.

---

## Building & Flashing

This project uses [PlatformIO](https://platformio.org/).

```bash
# Clone the repository
git clone https://github.com/smlara/AstroK_Enfocador_Rotador.git
cd AstroK_Enfocador_Rotador

# Build and upload (NEMA 17 motor)
pio run -e astrok-c3 -t upload

# Or for the 35BY48 geared motor
pio run -e astrok-c3-35by48 -t upload

# Upload the web configuration page
pio run -e astrok-c3 -t uploadfs
```

Once a board has firmware, later updates can be done from the ASCOM app over USB.

### ASCOM driver

The Windows driver and configuration app live in [ascom-driver/](ascom-driver/). Build the installers with `ascom-driver/build.ps1`; they require the [ASCOM Platform 6](https://ascom-standards.org/).

### Dependencies (auto-installed by PlatformIO)

- `waspinator/AccelStepper ^1.64`
- `adafruit/Adafruit SSD1306 ^2.5.13`
- `adafruit/Adafruit GFX Library ^1.11.11`
- `teemuatlut/TMCStepper ^0.7.3`
- `bblanchon/ArduinoJson ^7.0.4`

---

## Encoder Controls

| Action | Effect |
|--------|--------|
| Rotate CW | Move forward by the current step |
| Rotate CCW | Move backward by the current step |
| Short press | Cycle the step (x10 / x100 / x500 steps, or x0.1° / x1°) |
| Long press (≥ 3 s) | Reset the position to 0 |
| Hold at power-on | Force WiFi access point mode |

---

## License

This project uses a **dual licensing** model:

### Non-commercial use (open source)

- **Firmware and software** (all source code in `src/`, `data/`, `ascom-driver/` and `scripts/`): [GNU General Public License v3.0](LICENSE)
- **Hardware** (schematics, PCB designs, mechanical files in `pcb/`): [CERN Open Hardware Licence v2 - Strongly Reciprocal](LICENSE-HARDWARE)

You are free to use, study, modify, and share this project for **personal, educational, or non-commercial purposes**, provided that any derivative work remains open under the same licenses.

### Commercial use

**Any commercial use — including but not limited to manufacturing, selling, or integrating this design into a product or service — is strictly prohibited without prior written permission from the author.**

If you are interested in a commercial license, please open a [commercial inquiry issue](../../issues/new?labels=commercial-inquiry&title=Commercial+License+Inquiry) in this repository.

---

## Author

**Sergio Martínez** — Astrophotography enthusiast  
Questions, contributions, and commercial inquiries: open an [issue](../../issues) or start a [discussion](../../discussions).
