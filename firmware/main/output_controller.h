#pragma once

#include <stdint.h>

extern "C" {
#include "dm631.h"
}

// ---------------------------------------------------------------------------
// Single LED output layer.
//
// Two sources feed the DM631, and exactly one drives it at a time:
//   - HOME      (Matter / Apple Home): per-zone colour, used when idle.
//   - AMBILIGHT (Wi-Fi UDP):           full-frame colour, takes over while
//                                       frames are arriving.
//
// The controller owns ALL DM631 writes behind a mutex, so the Matter thread and
// the UDP task can never interleave a frame. The HOME colours are always kept
// up to date, so when Ambilight stops the previous Apple Home state is restored.
//
// HOME-path changes (on/off, brightness, colour) are eased with a non-blocking
// fade (see OUTPUT_FADE_MS): a lightweight 50 Hz esp_timer ramps the LEDs from
// their current colour to the new target; ticks with nothing to do are no-ops.
// Matter attributes are NOT delayed — HomeKit state updates instantly; only the
// light output eases, exactly like Philips Hue. Ambilight frames bypass the
// fade (they are already a live 30–60 fps stream), but the revert to Home after
// Ambilight ends fades smoothly.
// ---------------------------------------------------------------------------

// Fade duration for HOME-path transitions (on/off/brightness/colour), in ms.
// 400 ms matches the Philips Hue default. Set to 0 to disable fading.
#define OUTPUT_FADE_MS   400

void output_controller_init(void);

// HOME path (Matter): set one zone's final colour. Starts a fade towards it
// when Ambilight is not active; otherwise stored and restored (with a fade)
// when Ambilight ends.
void output_set_home_zone(uint8_t zone, dm631_color_t color);

// AMBILIGHT path (UDP): set the full zone frame. Activates Ambilight mode and
// pushes to the LEDs immediately (the fade engine stands down while active).
void output_set_ambilight_frame(const dm631_color_t frame[DM631_NUM_ZONES]);

// Call periodically from the UDP task (on its receive timeout): if Ambilight is
// active but no frame has arrived within the timeout window, revert to HOME by
// fading back to the stored Apple Home colours.
void output_tick(void);
