#include "output_controller.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "output";

// Revert to Home this long after the last Ambilight frame.
#define AMBILIGHT_TIMEOUT_MS  2000

// ---- Fade engine -----------------------------------------------------------
// Apple Home sends group on/off as 10 separate, sequential commands. Committing
// the LEDs on every command makes them switch one-by-one. Instead, each command
// sets a per-zone *target*; a ~50 Hz timer ramps the *displayed* value toward
// the target and pushes one frame per tick. Result: smooth Hue-like fades, and
// because all 10 fades overlap, the small per-command offsets become invisible
// so groups look synchronized. Matter/HomeKit state is unaffected — only the
// physical ramp is animated; attributes still update/report instantly.
#define FADE_TICK_MS       20                       // animation step (50 Hz)
// Fade duration is OUTPUT_FADE_MS (output_controller.h): full-range (off→on)
// transitions take that long; smaller changes finish proportionally sooner.
#if OUTPUT_FADE_MS > 0
// 12-bit (0..4095) channel step per tick for a full-range fade in OUTPUT_FADE_MS.
#define FADE_STEP_CALC     ((4095 * FADE_TICK_MS) / OUTPUT_FADE_MS)
#define FADE_STEP          (FADE_STEP_CALC > 0 ? FADE_STEP_CALC : 1)
#else
#define FADE_STEP          4095                     // fading disabled — jump to target
#endif

enum output_mode_t { MODE_HOME = 0, MODE_AMBILIGHT };

static dm631_color_t     s_home[DM631_NUM_ZONES];   // Apple Home target colours
static dm631_color_t     s_disp[DM631_NUM_ZONES];   // currently displayed (fading) value
static dm631_color_t     s_ambi[DM631_NUM_ZONES];   // latest Ambilight frame
static output_mode_t     s_mode          = MODE_HOME;
static int64_t           s_last_frame_us = 0;
static SemaphoreHandle_t s_mutex         = NULL;
static esp_timer_handle_t s_fade_timer   = NULL;

// Push the currently-active buffer to the DM631. Caller must hold s_mutex.
// Home mode shows the fading buffer; Ambilight is instant.
static void push_active_locked(void)
{
    const dm631_color_t *buf = (s_mode == MODE_AMBILIGHT) ? s_ambi : s_disp;
    for (int z = 0; z < DM631_NUM_ZONES; z++) {
        dm631_set_zone((uint8_t)z, buf[z]);
    }
    dm631_update();   // one atomic 384-bit frame for all zones
}

// Move *cur toward target by at most `step`. Returns true if it changed.
static inline bool step_toward(uint16_t *cur, uint16_t target, uint16_t step)
{
    if (*cur == target) return false;
    uint16_t diff = (target > *cur) ? (uint16_t)(target - *cur) : (uint16_t)(*cur - target);
    *cur = (diff <= step) ? target
                          : (uint16_t)((target > *cur) ? (*cur + step) : (*cur - step));
    return true;
}

// ~50 Hz: ramp the displayed buffer toward the Home target and push one frame
// if anything moved. Idle ticks (already at target) do nothing. Ambilight mode
// drives the LEDs directly, so the fade engine stands down there.
static void fade_tick(void *arg)
{
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_mode == MODE_HOME) {
        bool moved = false;
        for (int z = 0; z < DM631_NUM_ZONES; z++) {
            if (step_toward(&s_disp[z].r, s_home[z].r, FADE_STEP)) moved = true;
            if (step_toward(&s_disp[z].g, s_home[z].g, FADE_STEP)) moved = true;
            if (step_toward(&s_disp[z].b, s_home[z].b, FADE_STEP)) moved = true;
        }
        if (moved) {
            push_active_locked();
        }
    }
    xSemaphoreGive(s_mutex);
}

void output_controller_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    memset(s_home, 0, sizeof(s_home));
    memset(s_disp, 0, sizeof(s_disp));
    memset(s_ambi, 0, sizeof(s_ambi));
    s_mode = MODE_HOME;

    const esp_timer_create_args_t args = {
        .callback = &fade_tick,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "led_fade",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &s_fade_timer);
    esp_timer_start_periodic(s_fade_timer, FADE_TICK_MS * 1000);
}

void output_set_home_zone(uint8_t zone, dm631_color_t color)
{
    if (zone >= DM631_NUM_ZONES || !s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_home[zone] = color;   // new fade target; fade_tick ramps the LEDs toward it
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
    push_active_locked();   // Ambilight is real-time — no fade
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
            // Seed the displayed buffer with the last Ambilight frame so the
            // fade engine smoothly ramps it back to the stored Apple Home state.
            memcpy(s_disp, s_ambi, sizeof(s_disp));
            ESP_LOGI(TAG, "-> Home mode (ambilight idle %lld ms)", idle_ms);
        }
    }
    xSemaphoreGive(s_mutex);
}
