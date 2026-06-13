#include "dm631.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "dm631";

// Frame layout — matches the original woodenshark firmware
// (Lightpack-master/Firmware/LedDriver.c, hw6 block), empirically confirmed
// on this hardware 2026-06-09:
//   [0x000 pad] [z5.B z5.G z5.R] … [z9.B z9.G z9.R]   ← IC3 (far chip, first data sent)
//   [0x000 pad] [z0.B z0.G z0.R] … [z4.B z4.G z4.R]   ← IC2 (near chip, last data sent)
//   LATCH pulse on GPIO14
//
// Channel order per zone: B first, then G, then R.
// 32 values × 12 bits = 384 bits total, sent MSB-first via GPIO bit-bang.
#define FRAME_VALUES  32

static dm631_color_t s_zones[DM631_NUM_ZONES];
static uint16_t s_vals[FRAME_VALUES];

// Which physical DM631 zone (slot in s_zones, clocked out by build_frame) each
// LED socket is wired to.  Index = LED socket, 0-based (LED 1 = index 0).
// HomeKit "LED N" → this table → the zone that drives box socket N.
//
// Measured on this board 2026-06-12: the near DM631 (IC2, LEDs 1–5) is wired in
// a scrambled order; the far DM631 (IC3, LEDs 6–10) is already sequential.
//   LED 1 → Zone 4     LED 6  → Zone 5
//   LED 2 → Zone 3     LED 7  → Zone 6
//   LED 3 → Zone 0     LED 8  → Zone 7
//   LED 4 → Zone 1     LED 9  → Zone 8
//   LED 5 → Zone 2     LED 10 → Zone 9
static const uint8_t led_to_zone[DM631_NUM_ZONES] = {4, 3, 0, 1, 2, 5, 6, 7, 8, 9};

static void build_frame(void)
{
    int v = 0;

    // IC3 (far) — first 192 bits; they shift all the way through IC2 into IC3.
    s_vals[v++] = 0;
    for (int i = 5; i <= 9; i++) {
        s_vals[v++] = s_zones[i].b;
        s_vals[v++] = s_zones[i].g;
        s_vals[v++] = s_zones[i].r;
    }

    // IC2 (near, MOSI connected directly) — last 192 bits.
    s_vals[v++] = 0;
    for (int i = 0; i <= 4; i++) {
        s_vals[v++] = s_zones[i].b;
        s_vals[v++] = s_zones[i].g;
        s_vals[v++] = s_zones[i].r;
    }
}

esp_err_t dm631_init(void)
{
    memset(s_zones, 0, sizeof(s_zones));

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << DM631_GPIO_CLK)
                      | (1ULL << DM631_GPIO_MOSI)
                      | (1ULL << DM631_GPIO_LAT),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(DM631_GPIO_CLK,  0);
    gpio_set_level(DM631_GPIO_MOSI, 0);
    gpio_set_level(DM631_GPIO_LAT,  0);

    ESP_LOGI(TAG, "init OK (soft SPI) — CLK=GPIO%d MOSI=GPIO%d LAT=GPIO%d",
             DM631_GPIO_CLK, DM631_GPIO_MOSI, DM631_GPIO_LAT);

    // Push an all-zero frame so LEDs start off (clears any power-on garbage).
    dm631_update();

    return ESP_OK;
}

void dm631_set_zone(uint8_t led, dm631_color_t color)
{
    if (led < DM631_NUM_ZONES) {
        s_zones[led_to_zone[led]] = color;   // map LED socket → physical DM631 zone
    }
}

void dm631_set_all(dm631_color_t color)
{
    for (int i = 0; i < DM631_NUM_ZONES; i++) {
        s_zones[i] = color;
    }
}

esp_err_t dm631_update(void)
{
    build_frame();

    // Clock out 32 × 12 bits MSB-first.
    // SPI mode 0: CLK idle LOW, data sampled on rising CLK edge.
    // gpio_set_level() through the ESP32-C6 GPIO matrix adds sufficient
    // setup/hold time (~50 ns) — no explicit delays needed.
    for (int i = 0; i < FRAME_VALUES; i++) {
        uint16_t v = s_vals[i];
        for (int bit = 11; bit >= 0; bit--) {
            gpio_set_level(DM631_GPIO_MOSI, (v >> bit) & 1);
            gpio_set_level(DM631_GPIO_CLK, 1);
            gpio_set_level(DM631_GPIO_CLK, 0);
        }
    }

    // LATCH pulse: rising edge transfers shift register → output latches.
    gpio_set_level(DM631_GPIO_LAT, 1);
    gpio_set_level(DM631_GPIO_LAT, 0);

    return ESP_OK;
}
