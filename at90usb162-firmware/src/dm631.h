/*
 * dm631.h — DM631 LED driver for AT90USB162
 *
 * Bit-bang SPI, 12-bit per channel, 10 RGB zones.
 * Uses the same frame layout as the ESP32-C6 firmware (woodenshark rev6.x order).
 *
 * Default pin assignment — verify against your PCB schematic:
 *
 *   PB2 / MOSI  — DAI  (data in)
 *   PB1 / SCK   — CLK  (clock)
 *   PB0         — LAT  (latch)
 *
 * Pin assignments confirmed against Atarity/Lightpack Firmware/LedDriver.c
 * (hw6 block: LATCH_PIN(B,0), SCK_PIN(B,1), MOSI_PIN(B,2)).
 */

#pragma once

#include <avr/io.h>
#include <stdint.h>

/* ---- Pin definitions (adjust to match your schematic) -------- */

#define DM631_DAT_DDR   DDRB
#define DM631_DAT_PORT  PORTB
#define DM631_DAT_BIT   PB2     /* MOSI */

#define DM631_CLK_DDR   DDRB
#define DM631_CLK_PORT  PORTB
#define DM631_CLK_BIT   PB1     /* SCK  */

#define DM631_LAT_DDR   DDRB
#define DM631_LAT_PORT  PORTB
#define DM631_LAT_BIT   PB0     /* LATCH — confirmed hw6: LATCH_PIN(B,0) */

/* ---- Public types -------------------------------------------- */

/** 12-bit RGB colour (0 = off, 4095 = full brightness). */
typedef struct {
    uint16_t r;
    uint16_t g;
    uint16_t b;
} dm631_color_t;

#define DM631_NUM_ZONES  10   /**< 10 independent RGB zones. */

/* ---- Public API ---------------------------------------------- */

/** Configure GPIO pins and blank all zones. */
void dm631_init(void);

/** Update one zone's colour (does not push to hardware yet). */
void dm631_set_zone(uint8_t zone, dm631_color_t color);

/** Set every zone to the same colour (does not push yet). */
void dm631_set_all(dm631_color_t color);

/** Set all zones to black and immediately push to hardware. */
void dm631_off_all(void);

/** Clock the current frame into both DM631 ICs and pulse LAT. */
void dm631_update(void);
