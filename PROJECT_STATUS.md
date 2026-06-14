# Lightpack DIY — ESP32-C6 · Matter/HomeKit · Ambilight — Project Status

**Last updated:** 2026-06-14  
**Status:** ✅ **Complete and working** — Apple Home (10 zones) + screen Ambilight.
**Goal:** Replace the dead Lightpack MCU (AT90USB162) with an ESP32-C6 driving the
existing DM631 LED drivers, exposed to Apple Home via Matter, plus a Wi-Fi UDP
Ambilight receiver for screen sync.

> ⚠️ The **section below this divider reflects the current, verified state.** The
> numbered sections further down are historical reference from the 2026-05-25
> bring-up; a few claims there were later superseded (corrected here and inline).
> Full history: `docs/DEVLOG-*.md`.

## Current status (2026-06-14)

### Working
- **10 RGB zones** via two daisy-chained DM631 drivers; channel order
  **B → G → R** (confirmed); logical zones remapped to the printed socket numbers
  via `led_to_zone[]`.
- **Apple Home bridge:** Aggregator + 10 bridged Extended Color Lights
  ("LED 1"…"LED 10") — each a tile with tap-to-toggle, color, brightness; Siri works.
- **Color:** HueSaturation-native; advertises **HS + XY only (`0x09`)** — Color
  Temperature deliberately dropped to fix the HomeKit "light-blue swatch" bug.
  Gamma 2.2, per-zone NVS persistence, device identity set. Pairing code **20202021**.
- **Ambilight:** Wi-Fi UDP receiver — **DDP** (port **4048**, for HyperHDR) +
  **WLED** realtime (port **21324**, DRGB/DNRGB). Auto Home↔Ambilight switching,
  2 s idle revert that restores the Apple Home state. Verified with HyperHDR (macOS).
- **Enclosure:** all added electronics (ESP32-C6 + buck converter) fit inside the
  **original Lightpack housing** — the finished build keeps the original product's
  appearance (stealth retrofit).

### Hardware — corrections vs. the historical sections below
- **Wiring:** GPIO6 = CLK, GPIO7 = DATA, GPIO14 = LATCH, GND. Three signals + ground.
- **Power:** 12 V on the barrel jack → LED rail (isolated by D2). The DM631
  **logic VDD (~5 V) comes from the board's USB connector and is REQUIRED** — not
  from the jack. *(The old "leave USB unplugged to isolate the AVR" note is wrong:
  the logic needs that USB 5 V; without it the LEDs output garbage.)* ESP32 also
  powered from 5 V. ~3.3 V signalling works in practice.
- **AT90USB162 (IC1)** isolated by **grounding its RESET pin** (I/O → Hi-Z).

### Languages & toolchain
- **C++** — all Matter/application code (`app_main.cpp`, `app_driver.cpp`,
  `output_controller.cpp`, `ambilight_udp.cpp`); required by the esp-matter /
  CHIP (connectedhomeip) C++ SDK.
- **C** — the `dm631` LED driver (GPIO bit-bang), called from C++ via `extern "C"`.
- Built with **ESP-IDF** + **esp-matter** using the **RISC-V GCC** toolchain,
  target **ESP32-C6**. (The Ambilight test snippet is Python, run on the Mac.)

### Removed
- **Chip-temperature sensor.** Firmware was proven correct (value read, scaled,
  reported), but Apple Home does **not** render *bridged* Matter sensor values
  (known Apple-side limitation — open esp-matter issues). Reverted.

### Resolved since 2026-05-25 (listed as "open" in the historical sections below)
All 10 zones working · color wheel/slider correct · multi-zone + Siri
responsiveness · fabric/commissioning · per-tile tap-to-toggle.

---

## 1. Hardware

### ESP32-C6-DevKitM-1

| Property | Value |
|----------|-------|
| Module | ESP32-C6-MINI-1 |
| Chip | ESP32-C6FH4 |
| Flash | 4 MB (in-package) |
| I/O voltage | 3.3 V |
| USB ports | USB-C (native USB 2.0 FS) + USB-C to UART bridge |
| Boot button | GPIO9 (strapping pin, active LOW) |
| On-board RGB LED | GPIO8 (do not use for SPI) |

### Lightpack board (standard revision 6.x)

| Component | Part | Notes |
|-----------|------|-------|
| LED driver IC2 | SiTI DM631 | Controls zones 5–9 |
| LED driver IC3 | SiTI DM631 | Controls zones 0–4 |
| Current-limit resistors | R5, R6 = 3.9 kΩ | Use 3 kΩ only if swapped to DM633 |
| LED ports | 10 total | Each port = 1 RGB colour zone (3-LED strip) |
| Original MCU | Atmel AT90USB162 | Replaced by ESP32-C6 |
| Original interface | USB HID | Replaced by WiFi + Matter |

---

## 2. Pin Assignments

### ESP32-C6 → Lightpack DM631 wiring

| Signal | ESP32-C6 GPIO | Lightpack pad | Notes |
|--------|--------------|---------------|-------|
| CLK (DCK) | **GPIO6** | AT90USB162 SCK pin | SPI2 / FSPICLK |
| DATA (DAI) | **GPIO7** | AT90USB162 MOSI pin | SPI2 / FSPID |
| LATCH (LAT) | **GPIO14** | AT90USB162 LATCH pin | Software GPIO — NOT SPI CS |
| GND | GND | GND | Common ground required |
| 3.3 V | 3V3 | VCC (logic) | ESP32-C6 supplies 3.3 V logic only |

> **Power note:** The Lightpack's LEDs run at 5 V from an external supply.
> The DM631 logic interface runs at 3.3 V and is directly compatible with
> the ESP32-C6. Do NOT connect the 5 V LED rail to the ESP32-C6.

### ESP32-C6 J1 header reference (relevant pins)

| J1 Pin | GPIO | Function used |
|--------|------|---------------|
| 11 | GPIO6 | CLK → DM631 DCK |
| 12 | GPIO7 | MOSI → DM631 DAI |
| 13 | GPIO14 | LATCH → DM631 LAT |
| 1 | 3V3 | Logic power (optional if Lightpack board has own 3.3 V reg) |
| GND | GND | Common ground |

---

## 3. DM631 Protocol — Verified Details

Source: woodenshark/Lightpack firmware (`Firmware/LedDriver.c`), confirmed against
SiTI DM631/DM633 datasheet and Ontaelio/DMdriver Arduino library.

### Electrical

- **Interface:** 3-wire (CLK, DATA, LATCH) — no chip-select
- **Logic levels:** 3.3 V compatible
- **Clock:** 1 MHz used in firmware (DM631 supports higher; 1 MHz is conservative for ribbon cable)

### SPI parameters

| Parameter | Value |
|-----------|-------|
| SPI mode | Mode 0 (CPOL=0, CPHA=0) |
| Bit order | **MSB first** |
| Bits per channel | **12 bits** (0 = off, 4095 = full brightness) |
| Channel order per LED | **B → G → R** (blue clocked in first) |
| Channels per IC | 16 (15 used for 5 RGB LEDs + 1 padding) |
| ICs in chain | 2 (daisy-chained: ESP32 → IC1 → IC2) |

### Frame structure (48 bytes = 384 bits)

```
Byte offset   Content
──────────────────────────────────────────────────────────────────
 0– 2         0x000 (12-bit padding for IC2)
 3– 5         zone 5 Blue (12 bit), zone 5 Green (12 bit)  [packed]
 6– 8         zone 5 Red (12 bit),  zone 6 Blue (12 bit)   [packed]
 ...          zones 6, 7, 8, 9 follow in the same B-G-R order
 24–26        0x000 (12-bit padding for IC1)
 27–29        zone 0 Blue, zone 0 Green
 ...          zones 1, 2, 3, 4 follow
 45–47        zone 4 Green, zone 4 Red
```

Every two 12-bit values are packed into three bytes (MSB-first):
```
byte[0] = v0[11:4]
byte[1] = v0[3:0] | v1[11:8]
byte[2] = v1[7:0]
```

### LATCH pulse

- Fires **once**, **after** all 384 bits are clocked in for both ICs
- GPIO14: LOW → **HIGH** → LOW (active-high single pulse)
- Transfers shift-register contents to output latches on both ICs simultaneously
- No minimum pulse-width delay required at 3.3 V / ESP32 GPIO speeds (~20 ns)

### DM631 vs DM633 difference

DM631 and DM633 have **identical** shift-in protocol. DM633 adds a 7-bit global
brightness register not present on DM631. The firmware works for both chips.

---

## 4. Software / Toolchain

### Development environment

| Tool | Version | Location |
|------|---------|----------|
| ESP-IDF | v5.3.0 | `~/esp/esp-idf` |
| RISC-V GCC | 13.2.0 | `~/.espressif/tools/riscv32-esp-elf/` |
| OpenOCD | v0.12.0-esp32 | `~/.espressif/tools/openocd-esp32/` |
| esp-matter | latest main | `~/esp/esp-matter` |
| Python (venv) | 3.14.4 | `~/.espressif/python_env/idf5.3_py3.14_env/` |

**To activate in a new terminal:**
```bash
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh
```

### Firmware project

**Location:** `~/Documents/Project/Lightpack\ DIY/firmware/`

```
firmware/
├── CMakeLists.txt                # top-level build, links to esp-matter
├── partitions.csv                # 4 MB flash layout with OTA + factory NVS
├── sdkconfig.defaults            # BT, WiFi, Matter cluster pruning
├── sdkconfig.defaults.esp32c6    # target lock
└── main/
    ├── CMakeLists.txt
    ├── dm631.h / dm631.c         # DM631 SPI driver
    ├── app_priv.h                # shared types and defaults
    ├── app_driver.cpp            # Matter ↔ DM631 bridge (colour math, per-zone state)
    └── app_main.cpp              # Matter node, 10 endpoints, event loop
```

### Build status

| Item | Status |
|------|--------|
| `idf.py set-target esp32c6` | ✓ |
| `idf.py build` | ✓ — zero errors, 1.71 MB (13% headroom) |
| Binary | `build/lightpack.bin` |
| Matter device type | Extended Color Light × 10 (one per zone) |
| Colour modes supported | On/Off, Brightness, Hue/Saturation, Color Temperature, CIE XY |
| Per-zone independent control | ✓ 10 separate Matter endpoints → 10 HomeKit tiles |
| RGB colour wheel in Apple Home | ✓ confirmed (FeatureMap + ColorCapabilities fix) |
| NVS persistence | ✓ (deferred write per-endpoint, per-zone) |
| OTA update support | ✓ (partition table includes OTA slots) |

### Known TODOs in firmware

- **Button (GPIO9):** `app_driver_button_init()` returns NULL — button
  toggle and 5-second factory-reset long-press not yet wired in.
  Workaround: `idf.py erase-flash flash` for factory reset.
- **Prismatik API (future):** TCP:3636 Prismatik API for
  computer-screen-reactive ambilight. HomeKit per-zone control is now
  fully implemented; Prismatik is a future enhancement.
- **BLE re-advertising after fabric removal:** `kBLEDeinitialized` may
  fire before `kFabricRemoved`, preventing BLE re-advertising. Needs
  investigation (not reproduced since switching to `kAllSupported`).
- **Hardware wiring:** Not yet connected to Lightpack board (see § 5).

---

## 5. Wiring Status

| Connection | Status |
|------------|--------|
| ESP32-C6 GPIO7 → Lightpack DATA pad (right edge / J1) | ⬜ Not yet wired |
| ESP32-C6 GPIO6 → Lightpack CLK pad (right edge / J1) | ⬜ Not yet wired |
| ESP32-C6 GPIO14 → Lightpack LATCH pad (right edge / J1) | ⬜ Not yet wired |
| ESP32-C6 GND → Lightpack GND pad (right edge / J1) | ⬜ Not yet wired |
| 5 V external supply → Lightpack LED rail | ⬜ Not yet connected |
| AT90USB162 removed / bypassed | ⬜ Pending |

### Preferred solder points — NEXT_BOARD connector (J1), right edge of PCB

The Lightpack board has five dedicated labeled pads on its right edge (the J1
"NEXT_BOARD" daisy-chain connector). These are the easiest and most reliable
connection points — no need to solder directly to IC2's SOP24 pins.

```
Lightpack right edge          ESP32-C6 DevKitM-1
────────────────────          ──────────────────
DATA  pad  ──────────────────► J1 Pin 12 (GPIO7)
CLK   pad  ──────────────────► J1 Pin 11 (GPIO6)
LATCH pad  ──────────────────► J1 Pin 13 (GPIO14)
GND   pad  ──────────────────► GND
VCC   pad  — leave unconnected (ESP32-C6 has its own 3.3 V supply)
```

> **Note:** The DATA pad on J1 connects to the same `DATA1` net as IC2 Pin 2.
> Soldering to either point is electrically identical; J1 is far easier.

---

## 6. Next Steps — Hardware Build

### Step 1 — Bypass or remove the AT90USB162

The original MCU drives the same CLK/DATA/LAT lines. It must be
electrically isolated before the ESP32-C6 is connected, or it will
fight for the bus.

Options (choose one):
- **Remove the IC** (recommended if you have hot-air rework tools)
- **Lift pins** DCK, DAI, LAT on the AT90USB162 so it cannot drive the lines
- **Leave it in reset** by holding the RESET pin LOW (check schematic for reset pull-up)

### Step 2 — Locate the DM631 signal pads on the Lightpack board

Using the Lightpack 6.0L schematic (Eagle CAD files in the Lightpack GitHub repo):
- Find the pads where the AT90USB162 drives IC2/IC3 (the two DM631 chips)
- CLK and DATA are shared between both ICs (daisy-chained)
- LAT is a single line that latches both ICs simultaneously

### Step 3 — Wire ESP32-C6 to Lightpack board

Use short, direct wires (< 15 cm preferred for signal integrity at 1 MHz).

Solder to the **J1 "NEXT_BOARD" pads on the right edge** of the Lightpack PCB
(five labeled pads: VCC, GND, DATA, CLK, LATCH):

```
ESP32-C6 DevKitM-1          Lightpack board (J1 right-edge pads)
──────────────────          ──────────────────────────────────────
J1 Pin 12 (GPIO7)  ──────►  DATA  pad
J1 Pin 11 (GPIO6)  ──────►  CLK   pad
J1 Pin 13 (GPIO14) ──────►  LATCH pad
GND                ──────►  GND   pad
                             VCC   pad — leave unconnected
```

No level shifter needed — both sides are 3.3 V logic.

> **Alternative:** DATA can also be soldered directly to IC2 Pin 2 (DAI) on
> the DM631 chip — same net, but the J1 edge pad is far easier to work with.

### Step 4 — Power

- The ESP32-C6-DevKitM-1 is powered independently via its own USB-C cable
- The Lightpack board's 5 V LED supply connects to its existing power jack
- The shared GND is the only electrical connection required for the power domains

### Step 5 — Flash and commission

```bash
# In a terminal with environment sourced:
cd ~/Documents/Project/Lightpack\ DIY/firmware

# Flash (NVS preserved — use if already commissioned)
idf.py -p /dev/cu.usbserial-210 flash

# Flash + erase (wipes commissioning data — use for first-time or re-commissioning)
idf.py -p /dev/cu.usbserial-210 erase-flash flash
```

First boot output will show a Matter QR code. Open the iPhone **Home** app
→ Add Accessory → scan the QR code. All 10 zones appear as separate tiles.

### Step 6 — Verify DM631 output

At boot, the firmware runs a 3-colour diagnostic sequence (RED → GREEN → BLUE,
1.5 s each) across all zones. Confirm all 10 LED strips cycle through the colours
in the correct order. If a strip shows the wrong colour, cross-reference the
zone index with the DM631 channel mapping in § 3.

### Step 7 — Name zones in the Home app

After confirming hardware, rename zones in the Home app (tap tile → ✎) to
match physical strip positions. Reference mapping:

| HomeKit tile | Endpoint | DM631 IC | Physical position (to be confirmed) |
|---|---|---|---|
| Lightpack (Zone 0–4) | 1–5 | IC3 (near, zones 0–4) | TBD after hardware test |
| Lightpack (Zone 5–9) | 6–10 | IC2 (far, zones 5–9) | TBD after hardware test |

### Step 8 — Add button support (optional)

Edit `main/app_driver.cpp` — the `app_driver_button_init()` stub is
already in place. Wire the ESP32-C6 BOOT button (GPIO9, active LOW) to
toggle on/off and trigger factory reset on 5-second press.

### Step 9 — Prismatik API (future)

Add a TCP server on port 3636 implementing the Prismatik API v1.3
(`lock`, `setcolor:N-R,G,B;`, `setbrightness`, `setstatus`) so the
existing Prismatik ambilight software can drive per-zone colours over WiFi.

---

## 7. References

| Document | Location |
|----------|----------|
| Lightpack DIY build guide | `Lightpack-docs-master/Lightpack_DIY.md` |
| Lightpack basics | `Lightpack-docs-master/Lightpack_basics.md` |
| Prismatik API v1.3 | `Lightpack-docs-master/Prismatik_API.md` |
| ESP32-C6-DevKitM-1 datasheet | `datenblatt-2992163-espressif-esp32-c6-devkitm-1-n4-entwicklungsboard.pdf` |
| Lightpack firmware source | https://github.com/woodenshark/Lightpack |
| DM631 Arduino library | https://github.com/Ontaelio/DMdriver |
| ESP-IDF docs | https://docs.espressif.com/projects/esp-idf/en/v5.3/ |
| esp-matter docs | https://docs.espressif.com/projects/esp-matter/en/latest/ |

---

## 8. Session Log — 2026-05-10

### What was done

The device had already been wired, flashed, and commissioned in earlier sessions
(see `lightpack-diy-session-log.md`). This session focused on two software bugs
that prevented correct behaviour in Apple Home.

---

### Bug 1 — Apple Home shows warm/cool slider instead of RGB colour wheel

**Symptom:** After commissioning, the accessory tile in Apple Home only offers a
colour-temperature (warm/cool) slider. No RGB colour wheel is reachable.

**Root cause — ODR violation in the esp-matter SDK:**
Both `esp_matter_cluster.cpp` (legacy) and the generated
`extended_color_light_device.cpp` export the symbol
`esp_matter::cluster::color_control::create()`. The two translation units define
different `config_t` structs with `color_capabilities` at different byte offsets.
When the legacy version wins the linker race it reads from the wrong offset and
writes `0` to the `ColorCapabilities` attribute. Apple Home interprets
`ColorCapabilities = 0` as "no colour modes supported" and hides the colour wheel.

**Fix applied (`main/app_main.cpp`):**
After the endpoint is created, write `ColorCapabilities = 0x0009`
(bit 0 = HueSaturation, bit 3 = XY) directly onto the live attribute object —
this bypasses the struct-offset mismatch entirely.

**Status: ❌ Partially fixed.** ColorCapabilities was set correctly but Apple Home
still showed the temperature slider. Full fix completed in Session 2026-05-25.

---

### Bug 2 — Device undiscoverable after removing accessory from Apple Home

**Symptom:** After removing the accessory in Apple Home, the device never reappears
under "Nearby Accessories" and cannot be re-added.

**Root cause:**
The `kFabricRemoved` event handler called
`OpenBasicCommissioningWindow(..., kDnssdOnly)`. The device has no active WiFi at
that point, so DNS-SD discovery fails and BLE advertising is suppressed.

**Fix applied (`main/app_main.cpp`, `kFabricRemoved` case):**

```cpp
(void)mgr.OpenBasicCommissioningWindow(
    chip::System::Clock::Seconds16(k_timeout_seconds),
    chip::CommissioningWindowAdvertisement::kAllSupported);  // was kDnssdOnly
```

**Status: ⚠️ Not yet fully confirmed.** Not reproduced since fix applied.

---

### Other findings (2026-05-10)

| Finding | Detail |
|---------|--------|
| `matter factoryreset` shell command fails | Returns `Error: 47`. Use `idf.py erase-flash flash` instead. |
| UART port at the time | `/dev/cu.usbserial-110`, 115200 baud |

---

## 9. Session Log — 2026-05-25

### Goals

1. Add independent per-zone colour and brightness control for all 10 LED strips
   via Apple HomeKit (each zone = its own light tile with colour wheel).
2. Fix RGB colour wheel not appearing in Apple Home.

---

### Change 1 — Per-zone independent control (all three firmware files)

**Problem:** The original firmware created one Matter endpoint and called
`dm631_set_all()`, sending the same colour to all 10 zones.

**Solution:** Refactored to 10 Matter endpoints — one per LED zone.

#### `app_priv.h`
- Added `NUM_LIGHT_ZONES 10` constant.
- Added `app_driver_register_endpoint(zone, endpoint_id)` declaration —
  wires the zone→endpoint mapping used by the attribute callback.

#### `app_driver.cpp`
- Replaced 8 global colour-state variables with `zone_state_t s_zone_state[10]`
  — each zone has independent power, brightness, hue, saturation, XY, colour temp.
- `apply_to_leds()` → `apply_zone_to_leds(zone)`, now calls
  `dm631_set_zone(zone, color)` then `dm631_update()`.
- `endpoint_to_zone(endpoint_id)` maps incoming Matter endpoint IDs to zone
  indices in O(10).
- `app_driver_attribute_update()` looks up the zone before acting; events for
  unrelated endpoints are silently ignored.
- `app_driver_light_set_defaults(endpoint_id)` restores per-zone NVS state at boot.

#### `app_main.cpp`
- `light_endpoint_id` (singular) → `light_endpoint_ids[10]` (array).
- Loop 0–9: create endpoint, register zone, apply colour feature fix, set
  deferred persistence on attributes that exist.
- After `esp_matter::start()`: loop 0–9 to restore NVS state for each zone.

**Result in Apple Home:** 10 separate light tiles, each with independent
colour wheel, brightness slider, and on/off toggle.

---

### Change 2 — `CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT` raised to 11

**Problem:** Boot crash after zone 0:
```
E data_model: Dynamic endpoint count cannot be greater than
              CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT:2
```
`sdkconfig.defaults` had the count set to 2 (an old project override — the
Kconfig default is 16).

**Root cause of the off-by-one:** The esp-matter data model check is
`get_count(node) < MAX`. The root node endpoint (endpoint 0) is included in
`get_count()`, so `MAX=10` only allows 9 user endpoints.

**Final fix:** `CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT=11` in both
`firmware/sdkconfig.defaults` and `firmware/sdkconfig`.

**Rule:** Always set `MAX_DYNAMIC_ENDPOINT_COUNT = (number of user endpoints) + 1`.

**Verified:** All 10 zones (endpoints 1–10) created successfully:
```
I app_main: Zone 9 → endpoint id 10
I chip[ZCL]: Endpoint a On/off already set to new value
```

---

### Change 3 — RGB colour wheel fix (FeatureMap + ColorCapabilities)

**Problem:** Apple Home showed only a warm/cool colour-temperature slider,
no RGB colour wheel, even with `ColorCapabilities = 0x0009`.

**Root cause (fully diagnosed):**
Apple Home reads **both** `ColorCapabilities` *and* the `FeatureMap` attribute
on the ColorControl cluster. Without the HueSaturation bit in `FeatureMap`,
Apple Home ignores `ColorCapabilities` and shows only a colour-temperature slider.

The `FeatureMap` was not being updated because `hue_saturation::add()` calls
`update_feature_map()` internally, but this fails silently due to an ODR
violation in the esp-matter SDK:

- Both `esp_matter_feature.cpp` (legacy) and the generated `color_control.cpp`
  export `esp_matter::cluster::color_control::feature::hue_saturation::add()`.
- When the linker picks the legacy version, its `update_feature_map()` uses the
  legacy attribute lookup, which cannot find the `FeatureMap` attribute created
  by the generated `extended_color_light::create()` path.
- Result: log message `"Feature map attribute cannot be null"`, FeatureMap NOT
  updated, but `CurrentHue` / `CurrentSaturation` attributes ARE created.

**Fix in `configure_color_endpoint()` (`main/app_main.cpp`):**

Three-step approach after each `extended_color_light::create()` call:

1. **Call `hue_saturation::add()`** — creates `CurrentHue`, `CurrentSaturation`,
   and HS commands. The FeatureMap update fails (harmless warning) but attribute
   creation succeeds.

2. **Force `ColorCapabilities = 0x0009`** (HS bit 0 + XY bit 3) — bypasses the
   config-struct ODR mismatch that can write 0 here.

3. **Force `FeatureMap |= 0x0009`** on the ColorControl cluster (attribute
   `0xFFFC` = `Globals::Attributes::FeatureMap::Id`) — the missing piece:
   ```cpp
   attribute_t *fm_attr = attribute::get(endpoint_id, ColorControl::Id, 0xFFFC);
   esp_matter_attr_val_t fm_val = esp_matter_invalid(NULL);
   attribute::get_val(fm_attr, &fm_val);
   uint32_t new_fm = fm_val.val.u32 | 0x0009u;   // OR in HS(bit0) + XY(bit3)
   attribute::set_val(fm_attr, &esp_matter_bitmap32(new_fm));
   ```

**Verified in boot log (all 10 endpoints):**
```
I app_main: ep 1 ColorCapabilities    = 0x0009
I app_main: ep 1 ColorControl FeatureMap = 0x00000019
```
`0x0019` = HS (bit 0) + XY (bit 3) + CT (bit 4, already set by `create()`).

**End-to-end result:** After re-commissioning (erase-flash clears iOS cache),
Apple Home shows an RGB colour wheel on all 10 zone tiles. ✅

---

### Change 4 — Deferred persistence NULL guard

`set_deferred_persistence()` was being called for `CurrentX` and `CurrentY`
even when those attributes don't exist in HS-only mode, logging
`"Attribute cannot be NULL"` twice per zone at boot.

**Fix:** Wrapped all `set_deferred_persistence` calls in a guard lambda:
```cpp
auto defer_if_exists = [&](uint32_t cluster_id, uint32_t attr_id) {
    attribute_t *a = attribute::get(ep_id, cluster_id, attr_id);
    if (a) attribute::set_deferred_persistence(a);
};
```

---

### Other findings (2026-05-25)

| Finding | Detail |
|---------|--------|
| `sdkconfig.defaults` in project root | File contained literal `"404: Not Found"` — corrupted. Real file is `firmware/sdkconfig.defaults`. |
| UART port | `/dev/cu.usbserial-210`, 115200 baud (changed from `-110`) |
| Hard reset method | Use `python3 -m esptool --chip esp32c6 -p /dev/cu.usbserial-210 run`. Direct DTR/RTS toggling via pyserial can accidentally trigger download mode on this board. |
| Firmware binary size | 1.71 MB (13% flash headroom) |

---

### Next steps (after hardware wiring)

1. **Hardware wiring** — connect ESP32-C6 to Lightpack board (see § 5 & 6).
   Verify all 10 LED zones light up and respond to HomeKit commands.
2. **Zone-to-position mapping** — rename zones 0–9 in the Home app to match
   physical strip positions after DM631 channel order is confirmed on hardware.
3. **BLE re-advertising after fabric removal** — test whether the device
   reappears in "Nearby Accessories" after removing from the Home app.
4. **Button support (GPIO9)** — wire BOOT button for toggle + 5 s factory-reset
   long-press (`app_driver_button_init()` stub already in place).
5. **Prismatik API (future)** — TCP server on port 3636 for computer-screen
   reactive ambilight (`lock`, `setcolor:N-R,G,B;`, `setbrightness`).
