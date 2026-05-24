# Lightpack DIY — ESP32-C6 + Matter/HomeKit

> Replace the original Lightpack USB microcontroller (AT90USB162) with an ESP32-C6, driving the existing DM631 LED drivers over SPI and exposing the device to Apple HomeKit via the Matter protocol.

---

## What This Project Does

The [Lightpack](https://github.com/Atarity/Lightpack) is an open-source ambient lighting device originally controlled via USB by the Prismatik software. This project replaces the USB MCU entirely, turning the Lightpack into a standalone **Matter/HomeKit smart light** — no PC required.

- Control all 10 RGB LED zones from **Apple Home** and **Siri**
- Full colour support: Hue/Saturation, Brightness, Colour Temperature, CIE XY
- OTA firmware updates supported
- NVS persistence for light state across reboots

---

## Hardware

### Required

| Part | Notes |
|------|-------|
| Lightpack board (revision 6.x) | Original PCB with DM631 LED drivers |
| ESP32-C6-DevKitM-1 | ESP32-C6FH4, 4 MB flash, 3.3 V I/O |
| MP1584EN buck converter | Steps down Lightpack 5 V rail → 5 V for ESP32 |

### Lightpack Board ICs

| IC | Part | Zones |
|----|------|-------|
| IC2 | SiTI DM631 | Zones 5–9 |
| IC3 | SiTI DM631 | Zones 0–4 |

The AT90USB162 is **not removed** — it is simply isolated by leaving the USB1 (micro-USB) port unplugged. Without 5 V on USB1, the AVR never powers on.

---

## Wiring

### ESP32-C6 → Lightpack

| Signal | ESP32-C6 GPIO | Connection point |
|--------|--------------|-----------------|
| DATA | GPIO7 | IC2 Pin 2 (DAI) — soldered directly |
| CLK | GPIO6 | J2 expansion connector CLK pin |
| LATCH | GPIO14 | J2 expansion connector LATCH pin |
| GND | GND | MP1584 output GND (common ground) |
| 5 V in | — | MP1584 output → ESP32 5 V pin |

### Power

```
Lightpack barrel jack (5 V)
    ├── LED rail (Lightpack board)
    └── MP1584EN IN+
            └── MP1584EN OUT+ (5.0 V set) → ESP32-C6 5 V pin
```

> ⚠️ Never connect USB-C and the MP1584 output simultaneously.

### IC2 → IC3 Bridge (required for all 10 zones)

The PCB traces for CLK and LATCH between the two DM631 chips are broken. Solder two short jumper wires:

```
IC2 Pin 3  ──►  IC3 Pin 3   (CLK)
IC2 Pin 4  ──►  IC3 Pin 4   (LATCH)
```

Without this bridge only zones 5–9 (IC2 side) respond.

---

## DM631 Protocol

- **Interface:** 3-wire (CLK, DATA, LATCH), no chip-select
- **SPI mode:** Mode 0, MSB first, 1 MHz
- **Bits per channel:** 12 (0 = off, 4095 = full brightness)
- **Channel order per LED:** B → G → R (blue clocked in first)
- **Frame size:** 48 bytes (384 bits) for both ICs in chain
- **Driver:** GPIO bit-bang (hardware SPI/DMA causes byte-order corruption with 12-bit packing)

---

## Firmware

### Toolchain

| Tool | Version |
|------|---------|
| ESP-IDF | v5.3.0 |
| esp-matter | latest main |
| RISC-V GCC | 13.2.0 |

### Setup

```bash
# Activate toolchain (new terminal)
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh

# Build
cd ~/Documents/Project/Lightpack\ DIY/firmware/
idf.py set-target esp32c6
idf.py build

# Flash
idf.py -p /dev/cu.usbserial-110 flash monitor
```

### Project Structure

```
firmware/
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32c6
└── main/
    ├── dm631.h / dm631.c       # DM631 GPIO bit-bang driver
    ├── app_priv.h              # shared types and defaults
    ├── app_driver.cpp          # Matter ↔ DM631 bridge (colour math)
    └── app_main.cpp            # Matter node, endpoint, event loop
```

### Matter Device

- **Device type:** Extended Color Light
- **Colour modes:** On/Off, Brightness, Hue/Saturation, Colour Temperature, CIE XY
- **Commissioning:** BLE (no WiFi required for pairing)
- **`ColorCapabilities`:** manually set to `0x0009` (bits 0 + 3) to work around an ODR issue in the esp-matter SDK

---

## Current Status

| Item | Status |
|------|--------|
| Hardware wired | ✅ |
| AT90USB162 isolated | ✅ |
| Firmware builds | ✅ |
| 4 zones (IC2) working from HomeKit | ✅ |
| 6 zones (IC3) | ⬜ Needs IC2→IC3 CLK/LAT bridge |
| Colour wheel in Apple Home | ⬜ Under investigation |
| BLE re-advertising after fabric removal | ⬜ Under investigation |

---

## Known Issues

**Only 4 of 10 zones light up**
IC3 is not receiving CLK and LATCH signals. Solder the IC2→IC3 bridge wires described above.

**Apple Home shows warm/cool slider instead of colour wheel**
`ColorCapabilities = 0x0009` is set at boot but Apple Home may cache commissioning data. Fix: full `idf.py erase-flash flash` then re-add accessory.

**Device not discoverable after removing from Apple Home**
`kFabricRemoved` fires after BLE memory is already reclaimed. Investigating whether BLE can be re-initialized on demand.

**Factory reset via shell fails**
`matter factoryreset` returns `Error: 47`. Use `idf.py erase-flash flash` instead.

---

## References

- [Lightpack original firmware](https://github.com/woodenshark/Lightpack)
- [Lightpack hardware (revision 6)](https://github.com/Atarity/Lightpack-hardware)
- [Lightpack DIY schematic](https://github.com/Atarity/Lightpack-docs/blob/master/EN/Lightpack_DIY.md)
- [HomeSpan library](https://github.com/HomeSpan/HomeSpan)
- [esp-matter SDK](https://github.com/espressif/esp-matter)
- [ESP32-C6-DevKitM-1 pinout](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitm-1/)
- [SiTI DM631 datasheet](https://www.siti.com.tw/product/spec/LED/DM631.pdf)