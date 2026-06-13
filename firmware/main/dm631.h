#pragma once
#include <stdint.h>
#include "esp_err.h"

// SPI2 (FSPI) — CLK and MOSI only, no MISO, no hardware CS
#define DM631_GPIO_CLK   6    // FSPICLK
#define DM631_GPIO_MOSI  7    // FSPID / DAI
// Latch is a plain GPIO toggled in software after the full SPI frame.
// It is NOT part of the SPI peripheral — the DM631 latch fires on a
// rising edge and must arrive only after all 384 bits are clocked in.
#define DM631_GPIO_LAT   14

#define DM631_NUM_ZONES  10   // 10 individually controllable color zones

// 12-bit per channel (0 = off, 4095 = full brightness)
typedef struct {
    uint16_t r;
    uint16_t g;
    uint16_t b;
} dm631_color_t;

esp_err_t dm631_init(void);
void      dm631_set_zone(uint8_t led, dm631_color_t color);  // LED socket 0–9 (LED 1 = 0)
void      dm631_set_all(dm631_color_t color);
esp_err_t dm631_update(void);   // send frame then pulse LAT
