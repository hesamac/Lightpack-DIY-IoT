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

// ---------- per-zone light state ----------

struct zone_state_t {
    bool     power;
    float    brightness;  // 0–1
    float    hue;         // degrees 0–360
    float    saturation;  // 0–1
    uint16_t x;           // CIE 1931 X (0–65535)
    uint16_t y;           // CIE 1931 Y (0–65535)
    uint32_t temp_k;      // Kelvin
    uint8_t  color_mode;
};

static zone_state_t s_zone_state[NUM_LIGHT_ZONES];

// endpoint_id → zone index mapping (registered from app_main)
static uint16_t s_endpoint_ids[NUM_LIGHT_ZONES];
static bool     s_endpoints_registered = false;

void app_driver_register_endpoint(uint8_t zone, uint16_t endpoint_id)
{
    if (zone < NUM_LIGHT_ZONES) {
        s_endpoint_ids[zone] = endpoint_id;
        ESP_LOGI(TAG, "zone %d → endpoint %d", zone, endpoint_id);
    }
    s_endpoints_registered = true;
}

static int8_t endpoint_to_zone(uint16_t endpoint_id)
{
    for (int i = 0; i < NUM_LIGHT_ZONES; i++) {
        if (s_endpoint_ids[i] == endpoint_id) return (int8_t)i;
    }
    return -1;  // not one of our light endpoints
}

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

// Compute RGB from the given zone's state and push it to the DM631 hardware.
static void apply_zone_to_leds(uint8_t zone)
{
    zone_state_t &z = s_zone_state[zone];
    float r = 0.0f, g = 0.0f, b = 0.0f;

    ESP_LOGD(TAG, "zone %d: power=%d mode=%d bri=%.3f hue=%.1f sat=%.3f",
             zone, z.power, z.color_mode, z.brightness, z.hue, z.saturation);

    if (z.power) {
        if (z.color_mode == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
            hsv_to_rgb(z.hue, z.saturation, z.brightness, &r, &g, &b);
        } else if (z.color_mode == (uint8_t)ColorControl::ColorMode::kColorTemperature) {
            kelvin_to_rgb(z.temp_k, &r, &g, &b);
            r *= z.brightness; g *= z.brightness; b *= z.brightness;
        } else {
            xy_to_rgb(z.x, z.y, &r, &g, &b);
            r *= z.brightness; g *= z.brightness; b *= z.brightness;
        }
    }

    dm631_color_t color = {
        .r = (uint16_t)(r * 4095.0f),
        .g = (uint16_t)(g * 4095.0f),
        .b = (uint16_t)(b * 4095.0f),
    };

    ESP_LOGD(TAG, "zone %d → DM631 R=%u G=%u B=%u", zone, color.r, color.g, color.b);

    dm631_set_zone(zone, color);
    dm631_update();   // sends the full 384-bit frame for all zones atomically
}

// ---------- Matter attribute callback ----------

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle,
                                      uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    int8_t zone = endpoint_to_zone(endpoint_id);
    if (zone < 0) return ESP_OK;   // not one of our light endpoints

    zone_state_t &z = s_zone_state[zone];

    if (cluster_id == OnOff::Id) {
        if (attribute_id == OnOff::Attributes::OnOff::Id) {
            z.power = val->val.b;
            apply_zone_to_leds((uint8_t)zone);
        }
    } else if (cluster_id == LevelControl::Id) {
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            z.brightness = REMAP_TO_RANGE(val->val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS) / 100.0f;
            apply_zone_to_leds((uint8_t)zone);
        }
    } else if (cluster_id == ColorControl::Id) {
        if (attribute_id == ColorControl::Attributes::ColorMode::Id) {
            z.color_mode = val->val.u8;
            // No apply — mode change alone doesn't alter output until the
            // corresponding colour attribute arrives in the same transaction.
        } else if (attribute_id == ColorControl::Attributes::CurrentHue::Id) {
            z.hue = REMAP_TO_RANGE(val->val.u8, MATTER_HUE, STANDARD_HUE);
            apply_zone_to_leds((uint8_t)zone);
        } else if (attribute_id == ColorControl::Attributes::CurrentSaturation::Id) {
            z.saturation = REMAP_TO_RANGE(val->val.u8, MATTER_SATURATION, STANDARD_SATURATION) / 100.0f;
            apply_zone_to_leds((uint8_t)zone);
        } else if (attribute_id == ColorControl::Attributes::ColorTemperatureMireds::Id) {
            if (val->val.u16 > 0) {
                z.temp_k = 1000000UL / val->val.u16;
            }
            apply_zone_to_leds((uint8_t)zone);
        } else if (attribute_id == ColorControl::Attributes::CurrentX::Id) {
            z.x = val->val.u16;
            apply_zone_to_leds((uint8_t)zone);
        } else if (attribute_id == ColorControl::Attributes::CurrentY::Id) {
            z.y = val->val.u16;
            apply_zone_to_leds((uint8_t)zone);
        }
    }
    return ESP_OK;
}

// Read all light attributes from NVS-restored state and push to hardware at boot.
esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    int8_t zone = endpoint_to_zone(endpoint_id);
    if (zone < 0) return ESP_ERR_NOT_FOUND;

    zone_state_t &z = s_zone_state[zone];
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t *attr;

    attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
    attribute::get_val(attr, &val);
    z.color_mode = val.val.u8;

    attr = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    attribute::get_val(attr, &val);
    z.brightness = REMAP_TO_RANGE(val.val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS) / 100.0f;

    if (z.color_mode == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
        attribute::get_val(attr, &val);
        z.hue = REMAP_TO_RANGE(val.val.u8, MATTER_HUE, STANDARD_HUE);

        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
        attribute::get_val(attr, &val);
        z.saturation = REMAP_TO_RANGE(val.val.u8, MATTER_SATURATION, STANDARD_SATURATION) / 100.0f;

    } else if (z.color_mode == (uint8_t)ColorControl::ColorMode::kColorTemperature) {
        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorTemperatureMireds::Id);
        attribute::get_val(attr, &val);
        if (val.val.u16 > 0) {
            z.temp_k = 1000000UL / val.val.u16;
        }
    } else {
        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
        attribute::get_val(attr, &val);
        z.x = val.val.u16;

        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
        attribute::get_val(attr, &val);
        z.y = val.val.u16;
    }

    attr = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    attribute::get_val(attr, &val);
    z.power = val.val.b;

    apply_zone_to_leds((uint8_t)zone);
    return ESP_OK;
}

// ---------- init ----------

app_driver_handle_t app_driver_light_init(void)
{
    // Initialise per-zone state with power-on defaults.
    for (int i = 0; i < NUM_LIGHT_ZONES; i++) {
        s_zone_state[i] = {
            .power      = DEFAULT_POWER,
            .brightness = DEFAULT_BRIGHTNESS / (float)MATTER_BRIGHTNESS,
            .hue        = 0.0f,
            .saturation = 1.0f,
            .x          = 0,
            .y          = 0,
            .temp_k     = 4000,
            .color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation,
        };
    }

    dm631_init();
    return (app_driver_handle_t)(uintptr_t)1;   // non-null sentinel; dm631 is a singleton
}

app_driver_handle_t app_driver_button_init(void)
{
    // TODO: add BOOT button (GPIO9) toggle + long-press factory reset.
    // Skipped for initial build — use the chip shell: idf.py monitor → type 'matter factoryreset'
    return NULL;
}
