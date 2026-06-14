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
// ---------------------------------------------------------------------------

void output_controller_init(void);

// HOME path (Matter): set one zone's final colour. Pushed to the LEDs only when
// Ambilight is not active; otherwise stored and restored when Ambilight ends.
void output_set_home_zone(uint8_t zone, dm631_color_t color);

// AMBILIGHT path (UDP): set the full zone frame. Activates Ambilight mode and
// pushes to the LEDs immediately.
void output_set_ambilight_frame(const dm631_color_t frame[DM631_NUM_ZONES]);

// Call periodically from the UDP task (on its receive timeout): if Ambilight is
// active but no frame has arrived within the timeout window, revert to HOME and
// restore the stored Apple Home colours.
void output_tick(void);
