#include <esp_log.h>
#include <math.h>

#include <esp_matter.h>
#include <app_priv.h>
#include <common_macros.h>


extern "C" {
#include "dm631.h"
}

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t light_endpoint_id;

// ---------- retained light state ----------

static bool     s_power      = DEFAULT_POWER;
static float    s_brightness = DEFAULT_BRIGHTNESS / (float)MATTER_BRIGHTNESS;
static float    s_hue        = 0.0f;   // degrees 0–360
static float    s_saturation = 1.0f;   // 0–1
static uint16_t s_x          = 0;      // CIE 1931 X (0–65535)
static uint16_t s_y          = 0;      // CIE 1931 Y (0–65535)
static uint32_t s_temp_k     = 4000;   // Kelvin
static uint8_t  s_color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;

// ---------- color math ----------

// Standard HSV → RGB. h in degrees (0–360), s and v in 0–1. Outputs r,g,b in 0–1.
static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b)
{
    if (s == 0.0f) { *r = *g = *b = v; return; }
    float hh = fmodf(h, 360.0f) / 60.0f;
    int   i  = (int)hh;
    float f  = hh - i;
    float p  = v * (1.0f - s);
    float q  = v * (1.0f - s * f);
    float t  = v * (1.0f - s * (1.0f - f));
    switch (i) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default:*r = v; *g = p; *b = q; break;
    }
}

// Tanner Helland approximation: Kelvin (1000–40000) → RGB in 0–1.
static void kelvin_to_rgb(uint32_t k, float *r, float *g, float *b)
{
    float t = k / 100.0f;

    *r = (t <= 66.0f) ? 1.0f
                      : fminf(1.0f, fmaxf(0.0f, 329.698727f * powf(t - 60.0f, -0.13320476f) / 255.0f));

    *g = (t <= 66.0f) ? fminf(1.0f, fmaxf(0.0f, (99.470802f * logf(t) - 161.119568f) / 255.0f))
                      : fminf(1.0f, fmaxf(0.0f, 288.122170f * powf(t - 60.0f, -0.07551485f) / 255.0f));

    *b = (t >= 66.0f) ? 1.0f
       : (t <= 19.0f) ? 0.0f
                      : fminf(1.0f, fmaxf(0.0f, (138.517731f * logf(t - 10.0f) - 305.044793f) / 255.0f));
}

// CIE 1931 XY (Matter range 0–65535) → linear sRGB in 0–1, normalised to peak channel.
static void xy_to_rgb(uint16_t cx, uint16_t cy, float *r, float *g, float *b)
{
    float x = cx / 65535.0f;
    float y = (cy == 0) ? 0.001f : cy / 65535.0f;
    float z = 1.0f - x - y;
    float Y = 1.0f;
    float X = (Y / y) * x;
    float Z = (Y / y) * z;

    // Wide-gamut D65 XYZ → linear sRGB
    float lr =  X * 1.656492f - Y * 0.354851f - Z * 0.255038f;
    float lg = -X * 0.707196f + Y * 1.655397f + Z * 0.036152f;
    float lb =  X * 0.051713f - Y * 0.121364f + Z * 1.011530f;

    if (lr < 0) lr = 0;
    if (lg < 0) lg = 0;
    if (lb < 0) lb = 0;

    float peak = (lr > lg) ? ((lr > lb) ? lr : lb) : ((lg > lb) ? lg : lb);
    if (peak > 1.0f) { lr /= peak; lg /= peak; lb /= peak; }

    *r = lr; *g = lg; *b = lb;
}

// Compute RGB from current state and push to all 10 DM631 zones.
static void apply_to_leds(void)
{
    float r = 0.0f, g = 0.0f, b = 0.0f;

    ESP_LOGI(TAG, "apply_to_leds: power=%d mode=%d bri=%.3f hue=%.1f sat=%.3f",
             s_power, s_color_mode, s_brightness, s_hue, s_saturation);

    if (s_power) {
        if (s_color_mode == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
            hsv_to_rgb(s_hue, s_saturation, s_brightness, &r, &g, &b);
        } else if (s_color_mode == (uint8_t)ColorControl::ColorMode::kColorTemperature) {
            kelvin_to_rgb(s_temp_k, &r, &g, &b);
            r *= s_brightness; g *= s_brightness; b *= s_brightness;
        } else {
            xy_to_rgb(s_x, s_y, &r, &g, &b);
            r *= s_brightness; g *= s_brightness; b *= s_brightness;
        }
    }

    dm631_color_t color = {
        .r = (uint16_t)(r * 4095.0f),
        .g = (uint16_t)(g * 4095.0f),
        .b = (uint16_t)(b * 4095.0f),
    };
    ESP_LOGI(TAG, "dm631 → R=%u G=%u B=%u", color.r, color.g, color.b);
    dm631_set_all(color);
    dm631_update();
}

// ---------- Matter attribute callback ----------

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle,
                                      uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    ESP_LOGI(TAG, "attr_update: ep=%u cluster=0x%04lX attr=0x%04lX",
             endpoint_id, (unsigned long)cluster_id, (unsigned long)attribute_id);

    if (endpoint_id != light_endpoint_id) return ESP_OK;

    if (cluster_id == OnOff::Id) {
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            s_power = val->val.b;
            apply_to_leds();
        }
    } else if (cluster_id == LevelControl::Id) {
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            s_brightness = REMAP_TO_RANGE(val->val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS) / 100.0f;
            apply_to_leds();
        }
    } else if (cluster_id == ColorControl::Id) {
        if (attribute_id == ColorControl::Attributes::ColorMode::Id) {
            s_color_mode = val->val.u8;
            // No apply_to_leds — mode change alone doesn't alter output until
            // the corresponding color attribute arrives in the same transaction.
        } else if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            s_hue = REMAP_TO_RANGE(val->val.u8, MATTER_HUE, STANDARD_HUE);
            apply_to_leds();
        } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            s_saturation = REMAP_TO_RANGE(val->val.u8, MATTER_SATURATION, STANDARD_SATURATION) / 100.0f;
            apply_to_leds();
        } else if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            if (val->val.u16 > 0) {
                s_temp_k = 1000000UL / val->val.u16;
            }
            apply_to_leds();
        } else if (attribute_id == ColorControl::Attributes::CurrentX::Id) {
            s_x = val->val.u16;
            apply_to_leds();
        } else if (attribute_id == ColorControl::Attributes::CurrentY::Id) {
            s_y = val->val.u16;
            apply_to_leds();
        }
    }
    return ESP_OK;
}

// Read all light attributes from NVS-restored state and push to hardware at boot.
esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t *attr;

    attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
    attribute::get_val(attr, &val);
    s_color_mode = val.val.u8;

    attr = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attr, &val);
    s_brightness = REMAP_TO_RANGE(val.val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS) / 100.0f;

    if (s_color_mode == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
        attribute::get_val(attr, &val);
        s_hue = REMAP_TO_RANGE(val.val.u8, MATTER_HUE, STANDARD_HUE);

        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
        attribute::get_val(attr, &val);
        s_saturation = REMAP_TO_RANGE(val.val.u8, MATTER_SATURATION, STANDARD_SATURATION) / 100.0f;

    } else if (s_color_mode == (uint8_t)ColorControl::ColorMode::kColorTemperature) {
        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
        attribute::get_val(attr, &val);
        if (val.val.u16 > 0) {
            s_temp_k = 1000000UL / val.val.u16;
        }
    } else {
        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
        attribute::get_val(attr, &val);
        s_x = val.val.u16;

        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
        attribute::get_val(attr, &val);
        s_y = val.val.u16;
    }

    attr = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    attribute::get_val(attr, &val);
    s_power = val.val.b;

    apply_to_leds();
    return ESP_OK;
}

// ---------- init ----------

app_driver_handle_t app_driver_light_init(void)
{
    dm631_init();
    return (app_driver_handle_t)(uintptr_t)1;   // non-null sentinel; dm631 is a singleton
}

app_driver_handle_t app_driver_button_init(void)
{
    // TODO: add BOOT button (GPIO9) toggle + long-press factory reset.
    // Skipped for initial build — use the chip shell: idf.py monitor → type 'matter factoryreset'
    return NULL;
}
