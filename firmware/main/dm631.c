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

void dm631_set_zone(uint8_t zone, dm631_color_t color)
{
    if (zone < DM631_NUM_ZONES) {
        s_zones[zone] = color;
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
