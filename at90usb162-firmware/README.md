# Lightpack AT90USB162 Firmware

Clean-room reimplementation of the original Lightpack firmware for the
**AT90USB162** microcontroller found on the Lightpack rev 6.x DIY board.

The firmware presents the board as a USB HID device and accepts colour
commands from **Prismatik** (or any Lightpack-compatible software) to drive
the two **DM631** 12-bit LED driver ICs for all 10 RGB zones.

---

## Hardware

| IC  | Role                  | Zones  |
|-----|-----------------------|--------|
| IC2 | DM631 (near MCU)      | 0 – 4  |
| IC3 | DM631 (far, daisy-chained) | 5 – 9 |

### AT90USB162 → DM631 pin connections

| Signal | AT90USB162 pin | Connected to  |
|--------|---------------|---------------|
| DAI    | **PB2** (MOSI) | IC2 pin 2 (DAI) |
| CLK    | **PB1** (SCK)  | IC2 pin 3 (CLK) |
| LAT    | **PC2**        | IC2 pin 4 (LAT) |

> ⚠️ **Verify against your schematic.** If your board uses different pins,
> edit the `#define` statements at the top of `src/dm631.h`.

### IC2 → IC3 daisy-chain (Lightpack rev 6.x hardware bug)

The PCB traces for CLK and LAT between IC2 and IC3 are broken in revision 6.x.
Solder two short jumper wires:

```
IC2 pin 3  ──►  IC3 pin 3   (CLK)
IC2 pin 4  ──►  IC3 pin 4   (LAT)
```

Without the bridge only zones 0–4 (IC2 side) respond.

---

## USB Identity

| Property   | Value              |
|------------|--------------------|
| VID        | `0x03EB` (ATMEL)   |
| PID        | `0x2038` (Lightpack DIY) |
| Class      | HID                |
| Report IN  | 64 bytes           |
| Report OUT | 64 bytes           |

Prismatik detects the device automatically if the VID/PID is in its list.
If not, add `0x03EB:0x2038` under **Settings → Devices → Lightpack**.

---

## Protocol

Both directions use flat 64-byte HID reports (no report-ID byte).

### Host → Device (OUT report, 64 bytes)

| Byte  | Meaning                              |
|-------|--------------------------------------|
| `[0]` | Command byte (`CMD_*`)               |
| `[1…]`| Command payload (see table below)    |

| Command                   | Code | Payload                                     |
|---------------------------|------|---------------------------------------------|
| `CMD_UPDATE_LEDS`         | 1    | 10 × 3 bytes `[R G B]` (uint8, 0–255)       |
| `CMD_OFF_ALL`             | 2    | —                                           |
| `CMD_SET_BRIGHTNESS`      | 5    | `[1]` = brightness 0–100 %                  |
| `CMD_PING`                | 6    | —                                           |
| `CMD_GET_STATUS`          | 7    | —                                           |
| `CMD_GET_FIRMWARE_VERSION`| 8    | —                                           |
| `CMD_GET_SERIAL`          | 9    | —                                           |
| `CMD_LOCK_DEVICE`         | 13   | —                                           |
| `CMD_UNLOCK_DEVICE`       | 14   | —                                           |

**`CMD_UPDATE_LEDS` payload layout (bytes 1–30):**
```
byte 1 = R of zone 0   byte 2 = G of zone 0   byte 3 = B of zone 0
byte 4 = R of zone 1   byte 5 = G of zone 1   byte 6 = B of zone 1
…
byte 28 = R of zone 9  byte 29 = G of zone 9  byte 30 = B of zone 9
```
Each 8-bit value is scaled to 12-bit for the DM631: `dm631_val = uint8_val << 4`
(range 0–4080 out of 4095 maximum).

### Device → Host (IN report, 64 bytes)

| Byte  | Meaning                        |
|-------|--------------------------------|
| `[0]` | Echoed command byte            |
| `[1]` | Status (`0`=OK, `3`=Locked…)   |
| `[2…]`| Optional response data         |

---

## Build

### 1. Install the AVR toolchain (macOS)

```bash
brew tap osx-cross/avr
brew install avr-gcc avrdude
```

(`dfu-programmer` is already installed on this machine.)

### 2. Clone LUFA

Clone LUFA **inside** `at90usb162-firmware/` — the Makefile expects it at
`lufa-upstream/LUFA` by default:

```bash
cd "/Users/hesamac/Documents/Project/Lightpack DIY/at90usb162-firmware"
git clone https://github.com/abcminiuser/lufa.git lufa-upstream
```

`LUFA_PATH` defaults to `lufa-upstream/LUFA` (the `LUFA/` subdirectory
inside the repo, which contains `Drivers/`, `Build/`, `Common/`…).

To use an existing LUFA checkout elsewhere:
```bash
make LUFA_PATH=/absolute/path/to/existing/lufa-repo/LUFA
```

### 3. Build

```bash
cd "/Users/hesamac/Documents/Project/Lightpack DIY/at90usb162-firmware"
make
```

Expected output: `lightpack.hex` + size report (should be well under 8 KB flash
and 512 B SRAM).

---

## Flash

### Enter DFU bootloader

1. Hold the **HWB** button on the Lightpack board (the small tactile switch next
   to the USB socket).
2. Plug in the USB cable while still holding HWB.
3. Release HWB. The MCU boots into the built-in DFU bootloader.

Your OS will show a new device: **AT90USB162 DFU**.

### Flash with dfu-programmer (already installed)

```bash
make flash
```

This runs:
```bash
dfu-programmer at90usb162 erase  --force
dfu-programmer at90usb162 flash  lightpack.hex
dfu-programmer at90usb162 launch
```

After `launch` the MCU resets and enumerates as the Lightpack HID device.

---

## Relationship to the ESP32-C6 firmware

Both firmware projects co-exist in the same repository:

| Aspect      | AT90USB162 firmware (this)    | ESP32-C6 firmware (`../firmware/`) |
|-------------|-------------------------------|------------------------------------|
| Interface   | USB HID ↔ Prismatik           | Matter/HomeKit (BLE + Wi-Fi)       |
| Power source| USB bus power                 | 5 V from barrel jack               |
| Isolation   | Active when USB cable plugged | Active when barrel jack plugged    |

Both share the same DM631 frame format and zone ordering.

---

## Files

```
at90usb162-firmware/
├── Makefile               — avr-gcc build system
├── README.md              — this file
├── LUFA/                  — LUFA subdir (git clone here, see Build above)
└── src/
    ├── LUFAConfig.h       — LUFA compile-time options
    ├── Descriptors.h/.c   — USB device / config / HID descriptors
    ├── lightpack.h/.c     — Prismatik protocol handler
    ├── dm631.h/.c         — DM631 bit-bang SPI driver
    └── main.c             — MCU init, USB events, main loop
```
