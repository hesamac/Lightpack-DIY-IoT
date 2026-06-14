# Lightpack DIY — ESP32-C6 · Matter/HomeKit · Ambilight

> Replace the dead original Lightpack microcontroller (AT90USB162) with an
> **ESP32-C6**, driving the existing **DM631** LED drivers, and expose the device
> to **Apple Home (Matter)** *and* **screen-sync Ambilight** — on one chip, no
> extra hardware, no PC required for normal use.

**Status: working end-to-end.** ✅ 10 RGB zones controllable from Apple Home/Siri,
**and** live screen Ambilight via HyperHDR.

---

## What it does

The [Lightpack](https://github.com/Atarity/Lightpack) is an open-source ambient
backlight, originally a USB device driven by the (now-dead-on-macOS) Prismatik
app. This project rips out the USB MCU and turns the board into:

- A **Matter / Apple Home** smart light — **10 independent RGB zones**, each its
  own tile with tap-to-toggle, color, and brightness; controllable by Siri.
- A **wireless Ambilight** — a desktop tool (**HyperHDR**) streams screen-edge
  colors over Wi-Fi and the LEDs follow the screen in real time.

Both run in the same firmware and switch automatically: Ambilight takes over
while it's streaming, and the device returns to its Apple Home state ~2 s after
streaming stops.

---

## Hardware

| Part | Notes |
|------|-------|
| Lightpack board (rev 6.x) | Original PCB with two SiTI **DM631** LED drivers (10 zones) |
| ESP32-C6-DevKitM-1 | RISC-V, Wi-Fi 6 + BLE, 4 MB flash, 3.3 V I/O |
| 12 V supply | Powers the LED rail via the barrel jack |
| 5 V source (USB charger or MP1584 buck) | Powers the board **logic** + the ESP32 |

> 🧩 **Stealth retrofit:** all the added electronics (ESP32-C6 + buck converter)
> fit **inside the original Lightpack enclosure**, so the finished unit keeps the
> original product's look — it looks just like a stock Lightpack from the outside.

### Wiring — ESP32-C6 → Lightpack (3 signals + ground)

| Signal | ESP32-C6 | Lightpack tap point |
|--------|----------|---------------------|
| CLK    | GPIO6  | IC1 pin 15 / DM631 DCK |
| DATA   | GPIO7  | IC1 pin 16 / DM631 DAI |
| LATCH  | GPIO14 | IC1 pin 14 / DM631 /LAT |
| GND    | GND    | common ground (mandatory) |

> Only these three signals + GND go to the board. **Do not** connect the ESP32's
> 3.3 V/5 V pins to the Lightpack.

### Power — the key gotcha

The DM631 **logic** (VDD, ~5 V) is fed only from the board's **USB connector**,
*not* from the barrel jack (the jack/diode D2 feeds the LED rail only). So:

```
12 V barrel jack ──► LED anode rail (isolated by D2)
5 V (charger / MP1584) ──► board USB port ──► DM631 logic VDD (REQUIRED)
                       └──► ESP32-C6 power
GND ───────────────────── shared by all three
```

If the board's USB 5 V is missing, the DM631s have no logic power and the LEDs
output garbage. (3.3 V from the ESP works in practice despite the DM631's 5 V
logic-high spec; if you ever see flicker, a 74AHCT125 buffer or a 4.5 V logic
rail fixes the margin.)

### Retiring the AT90USB162 (IC1)

IC1 is disabled by **grounding its RESET pin** (all its I/O go high-impedance, so
it can't fight the ESP on the shared bus). Optionally cut IC1 pins 14/15/16 to
remove it from the bus entirely.

---

## DM631 driver

- 3-wire bit-bang (CLK/DATA/LATCH), no chip-select, MSB-first.
- 12 bits per channel (0–4095). Two DM631s daisy-chained, 32×12 bits per frame.
- **Channel order per zone: B → G → R** (matches the original woodenshark firmware).
- A small **`led_to_zone[]`** table maps logical zones to the socket numbers
  printed on the enclosure (the near chip is wired in a scrambled order).

> If only ~5 zones respond, the CLK/LATCH link between the two DM631s may need a
> jumper.

---

## Matter / Apple Home

- **Topology:** a Matter **bridge** — one Aggregator + **10 bridged Extended
  Color Lights** ("LED 1"…"LED 10"), so each zone is a first-class HomeKit
  accessory (own tile, tap-to-toggle, room, scenes).
- **Color:** HueSaturation-native (Apple drives color via `MoveToHueAndSaturation`).
  Advertises **HS + XY only** (`0x09`) — **Color Temperature is deliberately
  dropped** to dodge a HomeKit bug that paints the swatch a default light-blue
  for lights exposing CT+HS together.
- **Commissioning:** BLE. **Pairing code: `20202021`.**
- Gamma 2.2, per-zone NVS persistence, device identity (manufacturer/model/serial).

---

## Ambilight (screen sync)

- A Wi-Fi UDP receiver listens on **two** protocols:
  - **DDP** (port **4048**) — recommended; what **HyperHDR**'s "DDP" device uses.
  - **WLED** realtime (port **21324**, DRGB/DNRGB) — for WLED-native tools + testing.
- Auto Home↔Ambilight switching with a 2 s idle revert that restores the Apple
  Home colors.

### HyperHDR setup (macOS)

1. Install **HyperHDR** (`…-macOS-arm64.dmg` for Apple Silicon) from its
   [GitHub releases](https://github.com/awawa-dev/HyperHDR/releases). First launch:
   right-click → Open; grant **Screen Recording** permission.
2. **LED Layout** → *Classic*, edge counts summing to **10**.
3. **LED Controller** → type **DDP**, host = ESP32 IP, port **4048**, order **RGB**.
4. Enable the macOS screen grabber. If dim, raise HyperHDR brightness/gamma.

Quick firmware-only test (no HyperHDR) — WLED broadcast "all red":
```bash
python3 -c "
import socket, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
pkt = bytes([2,2]) + bytes([255,0,0])*10
for _ in range(50):
    s.sendto(pkt, ('255.255.255.255', 21324)); time.sleep(0.1)
"
```

---

## Build & flash

```bash
# Each new terminal:
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh    # needed for builds (sets ESP_MATTER_PATH)

cd firmware
idf.py set-target esp32c6            # first time only
idf.py -p /dev/cu.usbserial-XXX build flash monitor
```

- Run `idf.py` from `firmware/` (the repo-root `CMakeLists.txt` is a junk
  placeholder). The serial port name varies with the USB socket — check
  `ls /dev/cu.*`. If "port busy": `lsof -t /dev/cu.usbserial-XXX | xargs kill`.

### Firmware layout (`firmware/main/`)

```
dm631.{c,h}              DM631 bit-bang driver + zone→socket remap
app_priv.h               shared types / defaults
app_driver.cpp           Matter color math → output layer
app_main.cpp             Matter node, bridge endpoints, Wi-Fi, startup
output_controller.{cpp,h}  single LED output layer: Home/Ambilight mode + mutex
ambilight_udp.{cpp,h}    Wi-Fi UDP receiver (WLED 21324 + DDP 4048)
```

### Languages & toolchain

- **C++** — all Matter/application code (`app_main`, `app_driver`,
  `output_controller`, `ambilight_udp`); the esp-matter / CHIP (connectedhomeip)
  SDK is C++, so the app is too.
- **C** — the `dm631` LED driver (GPIO bit-bang), called from C++ via `extern "C"`.
- Built with **ESP-IDF** + **esp-matter**, **RISC-V GCC** toolchain, target
  **ESP32-C6**. (The Ambilight test snippet is Python and runs on the Mac, not
  the device.)

---

## Status

| Item | Status |
|------|--------|
| Hardware bring-up (10 zones, correct colors) | ✅ |
| Apple Home: 10 zones, color/brightness, per-tile toggle | ✅ |
| Color slider correct (light-blue bug fixed) | ✅ |
| Multi-light / Siri responsiveness | ✅ |
| Device identity (manufacturer/model/serial) | ✅ |
| Ambilight (WLED + DDP), HyperHDR verified | ✅ |
| Chip-temperature sensor | ❌ removed — Apple Home won't render bridged Matter sensors |

See [`docs/`](docs/) for the detailed dev logs.

---

## Safety

This is a do-it-yourself project that modifies the electronics of a powered
device. **Build and use it at your own risk.**

- **Power rails:** the LED rail runs at **12 V** (barrel jack); the board logic +
  ESP32 run at **5 V** (board USB). Don't cross-wire the rails, and never feed
  12 V into the ESP32 or its GPIOs.
- **Common ground:** all supplies (12 V, 5 V, ESP32) must share a ground.
- **LED current** flows through the DM631 drivers and the 12 V rail — *not*
  through the ESP32. Size the 12 V supply for your LEDs.
- **Heat:** the electronics sit inside the original enclosure; ensure adequate
  cooling. There is **no firmware thermal protection** (that feature was removed).
- This is a hobby project, **not a certified or commercial product**.

---

## Disclaimer

This is an independent, non-commercial DIY project. It is **not affiliated with,
endorsed by, or certified by** the original Lightpack project / woodenshark,
Apple, the Connectivity Standards Alliance (Matter), Espressif, WLED, or HyperHDR.
All trademarks belong to their respective owners.

The firmware uses Matter **test** vendor/product IDs and is **not** a certified
Matter or "Works with Apple Home" product — references to "Apple Home" and
"Matter" describe local compatibility only. The software is provided "as is",
without warranty of any kind (see [`LICENSE`](LICENSE)).

---

## Credits & upstream

This project builds on the original **Lightpack** by woodenshark / Atarity — the
ambient-light hardware, the original AVR firmware, and its documentation. Those
upstream sources are **GPL/CC licensed and are not bundled in this repository**;
get them from the original project:

- Original Lightpack: <https://github.com/Atarity/Lightpack>
- Original firmware: <https://github.com/woodenshark/Lightpack>

This repo contains **only the new ESP32-C6 firmware**. The DM631 driver is a
clean re-implementation; the original `LedDriver.c` is credited in the source
comments.

---

## License

This project's own code (the ESP32-C6 firmware in `firmware/`) is released under
the **MIT License** — see [`LICENSE`](LICENSE). Third-party components (ESP-IDF,
esp-matter, etc.) retain their own licenses, and the original Lightpack sources
are not included here (see **Credits & upstream** above).

---

## References

- [Lightpack original firmware](https://github.com/woodenshark/Lightpack)
- [esp-matter SDK](https://github.com/espressif/esp-matter)
- [HyperHDR](https://github.com/awawa-dev/HyperHDR)
- [ESP32-C6-DevKitM-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitm-1/)
- [SiTI DM631 datasheet](https://www.siti.com.tw/product/spec/LED/DM631.pdf)
