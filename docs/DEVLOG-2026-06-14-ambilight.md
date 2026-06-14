# Lightpack DIY — Dev Log: Ambilight + Temp-sensor attempt (2026-06-14)

Adding the **screen-sync Ambilight** feature (the original Lightpack purpose)
to the working Matter/Apple-Home controller, plus an attempt at a chip-temp
sensor (added, then removed).

**Result:** Ambilight works end-to-end — HyperHDR on macOS drives the 10 zones
from the screen in real time, while Apple Home control is fully preserved
(no re-pair, same Wi-Fi transport).

---

## Commits

| Commit | What |
|--------|------|
| `fb5b6fe` | Wi-Fi UDP ambilight (WLED) + Home/Ambilight mode switching |
| `aca6ee2` | Add DDP protocol to the receiver (for HyperHDR) |

(The temperature sensor work was implemented, verified-correct in firmware, then
**reverted** — see below — so it is not in the tree.)

---

## Architecture decision — Option B

Goal: support two modes in one firmware — **Home** (Apple Home via Matter) and
**Ambilight** (live screen colours) — and switch between them.

Options weighed:
- **A. Pure Thread** — best Home latency, but no Wi-Fi for Ambilight.
- **B. Matter-over-Wi-Fi + Wi-Fi UDP Ambilight** ← chosen.
- **C. Thread + Wi-Fi coexistence** — heaviest (two stacks share one 2.4 GHz
  radio → flash/RAM pressure + airtime contention).
- **D. Thread + USB-serial Ambilight** — great latency, but tethers the Mac.

**Chosen B** because: no migration, **no re-pair**, keeps the whole working
device (bridge, names, identity, light-blue fix) intact, lowest risk, fits the
4 MB flash, and Ambilight smoothness comes from **local UDP (~1–5 ms)** — not
from Matter — so dragging the Home colour wheel staying on Wi-Fi latency is
irrelevant (Ambilight is the dynamic-colour source).

Prismatik is dead on macOS 26; the modern sender is **HyperHDR**.

---

## Ambilight implementation

Two small modules, one clean output seam:

### `output_controller.{cpp,h}` — single LED output layer
- Two source buffers: **HOME** (per-zone, from Matter) and **AMBILIGHT**
  (full frame, from UDP). Exactly one drives the DM631 at a time.
- **Mutex-protected** — the Matter thread and the UDP task never interleave a
  384-bit frame.
- HOME colours are always kept current, so when Ambilight stops the previous
  Apple Home state is **restored**.
- Auto switching: a frame activates AMBILIGHT; **~2 s** with no frame reverts to
  HOME (`output_tick()` is called from the UDP task's receive timeout).
- `app_driver` was rerouted: `apply_zone_to_leds()` now hands its computed
  colour to `output_set_home_zone()` instead of writing the DM631 directly.

### `ambilight_udp.{cpp,h}` — UDP receiver task
- One task, `select()` on **two** sockets:
  - **21324 WLED** realtime: DRGB (proto 2) + DNRGB (proto 4).
  - **4048 DDP**: Distributed Display Protocol.
- 8-bit sRGB → 12-bit with **gamma 2.2** (matches the Home path).
- Robust parsing: length/protocol checks, malformed packets ignored, partial
  frames accumulate.

### The HyperHDR gotcha → DDP
HyperHDR's **"WLED" device requires the target to also answer a WLED HTTP
`/json` handshake** (LED-count discovery) — our UDP-only receiver can't, so the
WLED device type won't connect. **DDP needs no handshake**, so it's the clean
path for a custom device. We kept WLED too (for the broadcast test + WLED tools).

---

## Verified HyperHDR setup (macOS)

1. Install **HyperHDR** — `…-macOS-arm64.dmg` (Apple Silicon) from the GitHub
   releases. First launch: right-click → Open (un-notarized), and grant
   **Screen Recording** permission.
2. **LED Layout** tab → **Classic**, edge counts summing to **10** (e.g.
   3/3/2/2). This sets the LED count.
3. **LED Controller** → Type **DDP**, Host = ESP32 IP, Port **4048**, order
   **RGB**.
4. Enable the macOS screen grabber. Allow network access if prompted.
5. If it looks dim, raise HyperHDR brightness/gamma (our firmware already
   applies gamma 2.2).

Quick firmware-only test (no HyperHDR), WLED broadcast "all red":
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
Monitor shows `-> Ambilight mode`, then `-> Home mode (ambilight idle …)`.

---

## Temperature sensor — attempted and removed

Goal was chip-temperature monitoring + thermal safety, with a "Lightpack Temp"
sensor in Apple Home.

**What was proven (via chip-shell `attribute get` + monitor logs):** the
firmware was **correct** — the ESP32-C6 internal sensor was read, scaled ×100,
written to the Temperature Measurement cluster, and reported (value climbed
28→33 °C with warm-up, no errors). Apple Home still showed **0 °C**.

**Conclusion:** a **known Apple-side limitation** — Apple Home does not reliably
render **bridged** Matter sensor values (multiple open esp-matter issues; same
family as the bridged-colour quirk). Confirmed even after a clean re-pair.
Web research showed the common fixes (×100 scaling, dedicated endpoint) — both
of which we already did.

**Also noted:** the C6 internal sensor lacks precise factory calibration on this
unit — absolute reading was ~18 °C low and shifted with the chosen measurement
range; per Espressif it's reliable only for *trend*, not absolute precision.

**Decision:** the Home tile being the only broken part (the value was correct
internally), and the user not needing it, the whole feature was **reverted**
(`git checkout` + delete) back to `e42d959`.

---

## Final state

- ✅ Matter/Apple Home: 10 zones, colour/brightness, bridge, identity, light-blue
  fix — all intact, no re-pair.
- ✅ Ambilight: WLED (21324) + DDP (4048), auto Home↔Ambilight switching, 2 s
  revert with Home-state restore; HyperHDR verified.
- ⬜ Optional follow-ups: Home "Ambilight" on/off switch endpoint; make Home
  "Off" force-exit Ambilight; Step 7 cleanup (dedupe gamma, log levels).
- Binary ~1.72 MB / 1.875 MB partition (12 % free).
