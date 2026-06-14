#include "output_controller.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "output";

// Revert to Home this long after the last Ambilight frame.
#define AMBILIGHT_TIMEOUT_MS  2000

enum output_mode_t { MODE_HOME = 0, MODE_AMBILIGHT };

static dm631_color_t     s_home[DM631_NUM_ZONES];   // latest Apple Home colours
static dm631_color_t     s_ambi[DM631_NUM_ZONES];   // latest Ambilight frame
static output_mode_t     s_mode          = MODE_HOME;
static int64_t           s_last_frame_us = 0;
static SemaphoreHandle_t s_mutex         = NULL;

// Push the currently-active buffer to the DM631. Caller must hold s_mutex.
static void commit_locked(void)
{
    const dm631_color_t *buf = (s_mode == MODE_AMBILIGHT) ? s_ambi : s_home;
    for (int z = 0; z < DM631_NUM_ZONES; z++) {
        dm631_set_zone((uint8_t)z, buf[z]);
    }
    dm631_update();   // one atomic 384-bit frame for all zones
}

void output_controller_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    memset(s_home, 0, sizeof(s_home));
    memset(s_ambi, 0, sizeof(s_ambi));
    s_mode = MODE_HOME;
}

void output_set_home_zone(uint8_t zone, dm631_color_t color)
{
    if (zone >= DM631_NUM_ZONES || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_home[zone] = color;
    if (s_mode == MODE_HOME) {
        commit_locked();
    }
    xSemaphoreGive(s_mutex);
}

void output_set_ambilight_frame(const dm631_color_t frame[DM631_NUM_ZONES])
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(s_ambi, frame, sizeof(s_ambi));
    if (s_mode != MODE_AMBILIGHT) {
        s_mode = MODE_AMBILIGHT;
        ESP_LOGI(TAG, "-> Ambilight mode");
    }
    s_last_frame_us = esp_timer_get_time();
    commit_locked();
    xSemaphoreGive(s_mutex);
}

void output_tick(void)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_mode == MODE_AMBILIGHT) {
        int64_t idle_ms = (esp_timer_get_time() - s_last_frame_us) / 1000;
        if (idle_ms >= AMBILIGHT_TIMEOUT_MS) {
            s_mode = MODE_HOME;
            ESP_LOGI(TAG, "-> Home mode (ambilight idle %lld ms)", idle_ms);
            commit_locked();   // restore the stored Apple Home colours
        }
    }
    xSemaphoreGive(s_mutex);
}
