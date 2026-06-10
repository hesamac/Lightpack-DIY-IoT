/*
 * dm631.c — DM631 LED driver implementation for AT90USB162
 *
 * Frame layout (matches woodenshark Lightpack rev 6.x, verified against esp32 dm631.c):
 *
 *   [0x000 pad] [z5.B z5.R z5.G] … [z9.B z9.R z9.G]   ← IC3 (far,  zones 5–9)
 *   [0x000 pad] [z0.B z0.R z0.G] … [z4.B z4.R z4.G]   ← IC2 (near, zones 0–4)
 *   LATCH pulse
 *
 * Total: 32 × 12-bit = 384 bits.  Both ICs are daisy-chained; IC2 is the first IC
 * to receive data (closest to MCU), which then shifts it through to IC3.
 */

#include "dm631.h"
#include <string.h>

static dm631_color_t s_zones[DM631_NUM_ZONES];

/* ----------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------- */

/** Clock one 12-bit value MSB-first into the shift register (SPI mode 0). */
static void clock_val(uint16_t v)
{
    v &= 0x0FFF;
    for (int8_t bit = 11; bit >= 0; bit--) {
        if ((v >> bit) & 1)
            DM631_DAT_PORT |=  (1 << DM631_DAT_BIT);
        else
            DM631_DAT_PORT &= ~(1 << DM631_DAT_BIT);

        /* Rising edge — DM631 clocks data in on rising CLK edge. */
        DM631_CLK_PORT |=  (1 << DM631_CLK_BIT);
        /* Falling edge — CLK returns to idle. */
        DM631_CLK_PORT &= ~(1 << DM631_CLK_BIT);
    }
}

/**
 * Clock the entire 384-bit frame directly into the DM631 shift registers.
 * No intermediate buffer is used, saving 64 bytes of precious SRAM.
 *
 * Frame order (matches woodenshark Lightpack rev 6.x):
 *   [0x000] [z5.B z5.R z5.G] … [z9.B z9.R z9.G]   — IC3 (far,  zones 5–9)
 *   [0x000] [z0.B z0.R z0.G] … [z4.B z4.R z4.G]   — IC2 (near, zones 0–4)
 */
static void clock_frame(void)
{
    /* IC3 (far) — data for IC3 must travel through IC2 first */
    clock_val(0x000);
    for (uint8_t i = 5; i <= 9; i++) {
        clock_val(s_zones[i].b);
        clock_val(s_zones[i].r);
        clock_val(s_zones[i].g);
    }

    /* IC2 (near, directly connected to DAT pin) */
    clock_val(0x000);
    for (uint8_t i = 0; i <= 4; i++) {
        clock_val(s_zones[i].b);
        clock_val(s_zones[i].r);
        clock_val(s_zones[i].g);
    }
}

/* ----------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------- */

void dm631_init(void)
{
    memset(s_zones, 0, sizeof(s_zones));

    /* Configure DAT, CLK, LAT as outputs, all idle LOW. */
    DM631_DAT_DDR  |= (1 << DM631_DAT_BIT);
    DM631_CLK_DDR  |= (1 << DM631_CLK_BIT);
    DM631_LAT_DDR  |= (1 << DM631_LAT_BIT);

    DM631_DAT_PORT &= ~(1 << DM631_DAT_BIT);
    DM631_CLK_PORT &= ~(1 << DM631_CLK_BIT);
    DM631_LAT_PORT &= ~(1 << DM631_LAT_BIT);

    /* Push an all-zero frame so LEDs start off. */
    dm631_update();
}

void dm631_set_zone(uint8_t zone, dm631_color_t color)
{
    if (zone < DM631_NUM_ZONES)
        s_zones[zone] = color;
}

void dm631_set_all(dm631_color_t color)
{
    for (uint8_t i = 0; i < DM631_NUM_ZONES; i++)
        s_zones[i] = color;
}

void dm631_off_all(void)
{
    dm631_color_t off = {0, 0, 0};
    dm631_set_all(off);
    dm631_update();
}

void dm631_update(void)
{
    /* Clock the complete 384-bit frame into both DM631 shift registers. */
    clock_frame();

    /* LATCH pulse: rising edge transfers shift register → output latches. */
    DM631_LAT_PORT |=  (1 << DM631_LAT_BIT);
    DM631_LAT_PORT &= ~(1 << DM631_LAT_BIT);
}
