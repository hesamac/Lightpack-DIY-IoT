/*
 * dm631_test — standalone hardware validation for Lightpack DIY
 *
 * No Matter / HomeKit.  Just GPIO bit-bang → DM631 → LEDs.
 *
 * What to look for:
 *   Phase 1  ALL ZONES WHITE — if nothing lights up, it's a hardware/wiring problem.
 *   Phase 2  Zone sweep — each zone lights up RED then GREEN then BLUE in turn.
 *            Watch which physical strips respond and note the order.
 *   Phase 3  Brightness ramp — zone 0 fades white from dim to full.
 *
 * The test loops forever so you can observe each phase as many times as you like.
 * Press Ctrl+T Ctrl+R in the IDF monitor to reset / restart.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "dm631.h"

static const char *TAG = "dm631_test";

#define MS(x)  pdMS_TO_TICKS(x)

static const dm631_color_t OFF   = {0,    0,    0   };
static const dm631_color_t WHITE = {4095, 4095, 4095};
static const dm631_color_t RED   = {4095, 0,    0   };
static const dm631_color_t GREEN = {0,    4095, 0   };
static const dm631_color_t BLUE  = {0,    0,    4095};

void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  DM631 Hardware Test — Lightpack DIY  ");
    ESP_LOGI(TAG, "  CLK=GPIO%d  MOSI=GPIO%d  LAT=GPIO%d  ",
             DM631_GPIO_CLK, DM631_GPIO_MOSI, DM631_GPIO_LAT);
    ESP_LOGI(TAG, "  Zones: %d  (0-4 = IC2 near, 5-9 = IC3 far)",
             DM631_NUM_ZONES);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // dm631_init() already runs its own RGB diagnostic and clears all zones.
    // We skip it here and do our own controlled sequence.
    dm631_init();

    int loop = 0;

    while (1) {
        loop++;
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "===== TEST LOOP %d =====", loop);

        // ------------------------------------------------------------------
        // Phase 1: All zones full white — basic sanity check
        // ------------------------------------------------------------------
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "--- PHASE 1: ALL ZONES WHITE (3 s) ---");
        ESP_LOGI(TAG, "    => ALL LEDs should be on, full brightness white");
        dm631_set_all(WHITE);
        dm631_update();
        vTaskDelay(MS(3000));

        dm631_set_all(OFF);
        dm631_update();
        vTaskDelay(MS(500));

        // ------------------------------------------------------------------
        // Phase 2: Each zone individually — RED then GREEN then BLUE
        // ------------------------------------------------------------------
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "--- PHASE 2: Zone-by-zone sweep (1.0 s per colour) ---");

        for (int z = 0; z < DM631_NUM_ZONES; z++) {
            dm631_set_all(OFF);

            ESP_LOGI(TAG, "  Zone %d — RED   (IC%s)", z, z < 5 ? "2 near" : "3 far");
            dm631_set_zone(z, RED);
            dm631_update();
            vTaskDelay(MS(1000));

            ESP_LOGI(TAG, "  Zone %d — GREEN", z);
            dm631_set_zone(z, GREEN);
            dm631_update();
            vTaskDelay(MS(1000));

            ESP_LOGI(TAG, "  Zone %d — BLUE", z);
            dm631_set_zone(z, BLUE);
            dm631_update();
            vTaskDelay(MS(1000));

            dm631_set_zone(z, OFF);
            dm631_update();
            vTaskDelay(MS(300));
        }

        // ------------------------------------------------------------------
        // Phase 3: Brightness ramp on zone 0 (white), then zone 5
        // ------------------------------------------------------------------
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "--- PHASE 3: Brightness ramp, zone 0 then zone 5 ---");

        for (int target_zone = 0; target_zone <= 5; target_zone += 5) {
            ESP_LOGI(TAG, "  Ramping zone %d 0 → full → 0 (white)", target_zone);
            dm631_set_all(OFF);

            for (int lvl = 0; lvl <= 4095; lvl += 16) {
                dm631_color_t c = {lvl, lvl, lvl};
                dm631_set_zone(target_zone, c);
                dm631_update();
                vTaskDelay(MS(5));
            }
            for (int lvl = 4095; lvl >= 0; lvl -= 16) {
                dm631_color_t c = {lvl, lvl, lvl};
                dm631_set_zone(target_zone, c);
                dm631_update();
                vTaskDelay(MS(5));
            }
        }

        // ------------------------------------------------------------------
        // Phase 4: Rainbow across all zones simultaneously
        // ------------------------------------------------------------------
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "--- PHASE 4: Rainbow — one colour per zone ---");
        ESP_LOGI(TAG, "    zone 0=RED 1=GREEN 2=BLUE 3=CYAN 4=MAGENTA");
        ESP_LOGI(TAG, "    zone 5=YELLOW 6=RED 7=GREEN 8=BLUE 9=WHITE");

        static const dm631_color_t rainbow[DM631_NUM_ZONES] = {
            {4095, 0,    0   },   // 0 RED
            {0,    4095, 0   },   // 1 GREEN
            {0,    0,    4095},   // 2 BLUE
            {0,    4095, 4095},   // 3 CYAN
            {4095, 0,    4095},   // 4 MAGENTA
            {4095, 4095, 0   },   // 5 YELLOW
            {4095, 0,    0   },   // 6 RED
            {0,    4095, 0   },   // 7 GREEN
            {0,    0,    4095},   // 8 BLUE
            {4095, 4095, 4095},   // 9 WHITE
        };

        for (int z = 0; z < DM631_NUM_ZONES; z++) {
            dm631_set_zone(z, rainbow[z]);
        }
        dm631_update();
        vTaskDelay(MS(5000));

        // Blank before next loop
        dm631_set_all(OFF);
        dm631_update();

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "Loop %d done — pausing 2 s before next loop", loop);
        vTaskDelay(MS(2000));
    }
}
