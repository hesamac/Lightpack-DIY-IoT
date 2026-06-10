#include <esp_log.h>
#include <math.h>

#include <esp_matter.h>
#include <app_priv.h>
#include <common_macros.h>

#include <app-common/zap-generated/attributes/Accessors.h>

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

// RGB (0–1) → HSV. h in degrees 0–360, s and v in 0–1.
static void rgb_to_hsv(float r, float g, float b, float *h, float *s, float *v)
{
    float mx = fmaxf(r, fmaxf(g, b));
    float mn = fminf(r, fminf(g, b));
    float d  = mx - mn;

    *v = mx;
    *s = (mx == 0.0f) ? 0.0f : d / mx;

    if (d == 0.0f) {
        *h = 0.0f;
    } else if (mx == r) {
        *h = 60.0f * fmodf((g - b) / d, 6.0f);
    } else if (mx == g) {
        *h = 60.0f * ((b - r) / d + 2.0f);
    } else {
        *h = 60.0f * ((r - g) / d + 4.0f);
    }
    if (*h < 0.0f) *h += 360.0f;
}

// Linear RGB (0–1) → CIE 1931 xy in Matter range 0–65535.
// Inverse of the wide-gamut D65 matrix used in xy_to_rgb below.
static void rgb_to_xy(float r, float g, float b, uint16_t *out_x, uint16_t *out_y)
{
    float X = r * 0.664511f + g * 0.154324f + b * 0.162028f;
    float Y = r * 0.283881f + g * 0.668433f + b * 0.047685f;
    float Z = r * 0.000088f + g * 0.072310f + b * 0.986039f;

    float sum = X + Y + Z;
    if (sum <= 0.0f) { *out_x = 0; *out_y = 0; return; }

    *out_x = (uint16_t)(X / sum * 65535.0f);
    *out_y = (uint16_t)(Y / sum * 65535.0f);
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

    ESP_LOGI(TAG, "zone %d: power=%d mode=%d bri=%.3f hue=%.1f sat=%.3f",
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

    // Gamma correction (γ = 2.2): linearises perceived brightness and makes
    // colours look richer — without this, mid-range colours appear washed out.
    r = powf(r, 2.2f);
    g = powf(g, 2.2f);
    b = powf(b, 2.2f);

    dm631_color_t color = {
        .r = (uint16_t)(r * 4095.0f),
        .g = (uint16_t)(g * 4095.0f),
        .b = (uint16_t)(b * 4095.0f),
    };

    ESP_LOGI(TAG, "zone %d → DM631 R=%u G=%u B=%u", zone, color.r, color.g, color.b);

    dm631_set_zone(zone, color);
    dm631_update();   // sends the full 384-bit frame for all zones atomically
}

// ---------- helpers used by both callbacks ----------

// Re-read the full ColorControl state for one zone from the Matter data model
// and store it in s_zone_state[zone].  Does NOT call apply_zone_to_leds().
// Called both at boot (set_defaults) and from MatterPostAttributeChangeCallback.
static void sync_zone_color(uint8_t zone, uint16_t endpoint_id)
{
    zone_state_t &z  = s_zone_state[zone];
    attribute_t *attr;
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);

    attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::ColorMode::Id);
    if (attr && attribute::get_val(attr, &val) == ESP_OK) z.color_mode = val.val.u8;

    if (z.color_mode == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
        // Prefer EnhancedCurrentHue (16-bit, 0–65535) for full precision; the
        // 8-bit CurrentHue loses resolution and causes the Apple Home color
        // wheel to show a slightly wrong position when the detail view reopens.
        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::EnhancedCurrentHue::Id);
        if (attr && attribute::get_val(attr, &val) == ESP_OK) {
            z.hue = val.val.u16 * 360.0f / 65536.0f;
        } else {
            attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id);
            if (attr && attribute::get_val(attr, &val) == ESP_OK)
                z.hue = REMAP_TO_RANGE(val.val.u8, MATTER_HUE, STANDARD_HUE);
        }

        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id);
        if (attr && attribute::get_val(attr, &val) == ESP_OK)
            z.saturation = REMAP_TO_RANGE(val.val.u8, MATTER_SATURATION, STANDARD_SATURATION) / 100.0f;

    } else if (z.color_mode == (uint8_t)ColorControl::ColorMode::kColorTemperature) {
        attr = attribute::get(endpoint_id, ColorControl::Id,
                              ColorControl::Attributes::ColorTemperatureMireds::Id);
        if (attr && attribute::get_val(attr, &val) == ESP_OK && val.val.u16 > 0)
            z.temp_k = 1000000UL / val.val.u16;

    } else {   // XY mode
        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id);
        if (attr && attribute::get_val(attr, &val) == ESP_OK) z.x = val.val.u16;

        attr = attribute::get(endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id);
        if (attr && attribute::get_val(attr, &val) == ESP_OK) z.y = val.val.u16;
    }
}

// ---------- Matter attribute callback (WriteAttribute path) ----------

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
            ESP_LOGI(TAG, "zone %d OnOff → %d", zone, z.power);
            apply_zone_to_leds((uint8_t)zone);
        }
    } else if (cluster_id == LevelControl::Id) {
        if (attribute_id == LevelControl::Attributes::CurrentLevel::Id) {
            z.brightness = REMAP_TO_RANGE(val->val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS) / 100.0f;
            ESP_LOGI(TAG, "zone %d Level=%u → bri=%.3f", zone, val->val.u8, z.brightness);
            apply_zone_to_leds((uint8_t)zone);
        }
    } else if (cluster_id == ColorControl::Id) {
        // ColorControl attributes CAN arrive here if the controller sends WriteAttribute
        // directly (rare — most controllers send commands like MoveToHueAndSaturation).
        // The command path is handled by MatterPostAttributeChangeCallback below.
        ESP_LOGI(TAG, "zone %d ColorControl attr=0x%04" PRIx32 " via WriteAttribute",
                 zone, attribute_id);
        sync_zone_color((uint8_t)zone, endpoint_id);
        apply_zone_to_leds((uint8_t)zone);
    }
    return ESP_OK;
}

// ---------- colour attribute mirroring ----------
//
// The endpoint carries three parallel colour representations (HS, XY, CT) but
// the ColorControl server only writes the one being actively commanded.  The
// other two go stale, and after a reboot/re-prime Apple Home may render the
// slider from a stale representation (seen as a light-blue slider while the
// LED shows the real colour).  Mirror every change into the other
// representations via the Accessors API so any readback is consistent.
// CT is never written as a mirror target (arbitrary colours have no CT).

static bool s_mirroring = false;   // re-entry guard: Accessors fire our callback

static void mirror_color_attributes(uint8_t zone, uint16_t endpoint_id)
{
    zone_state_t &z = s_zone_state[zone];
    float r, g, b, h, s, v;
    uint16_t x, y;

    s_mirroring = true;

    if (z.color_mode == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
        // HS is authoritative → update XY.
        hsv_to_rgb(z.hue, z.saturation, 1.0f, &r, &g, &b);
        rgb_to_xy(r, g, b, &x, &y);
        ColorControl::Attributes::CurrentX::Set(endpoint_id, x);
        ColorControl::Attributes::CurrentY::Set(endpoint_id, y);

    } else if (z.color_mode == (uint8_t)ColorControl::ColorMode::kColorTemperature) {
        // CT is authoritative → update both HS and XY.
        kelvin_to_rgb(z.temp_k, &r, &g, &b);
        rgb_to_hsv(r, g, b, &h, &s, &v);
        ColorControl::Attributes::CurrentHue::Set(endpoint_id,
            (uint8_t)(h / STANDARD_HUE * MATTER_HUE));
        ColorControl::Attributes::CurrentSaturation::Set(endpoint_id,
            (uint8_t)(s * MATTER_SATURATION));
        rgb_to_xy(r, g, b, &x, &y);
        ColorControl::Attributes::CurrentX::Set(endpoint_id, x);
        ColorControl::Attributes::CurrentY::Set(endpoint_id, y);

    } else {
        // XY is authoritative → update HS.
        xy_to_rgb(z.x, z.y, &r, &g, &b);
        rgb_to_hsv(r, g, b, &h, &s, &v);
        ColorControl::Attributes::CurrentHue::Set(endpoint_id,
            (uint8_t)(h / STANDARD_HUE * MATTER_HUE));
        ColorControl::Attributes::CurrentSaturation::Set(endpoint_id,
            (uint8_t)(s * MATTER_SATURATION));
    }

    s_mirroring = false;
}

// Apple Home's Matter implementation renders the colour slider only from the
// XY or CT representation — a zone whose ColorMode is HueSaturation gets the
// generic light-blue fill no matter what CurrentHue/CurrentSaturation say.
// Freshly created zones default to HS mode and stay there until the first
// colour command (Apple sends MoveToColor, which switches the mode to XY and
// permanently fixes the rendering).  Normalise HS → XY at boot instead.
static void boot_normalize_color_mode(uint8_t zone, uint16_t endpoint_id)
{
    zone_state_t &z = s_zone_state[zone];

    if (z.color_mode == (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation) {
        float r, g, b;
        uint16_t x, y;
        hsv_to_rgb(z.hue, z.saturation, 1.0f, &r, &g, &b);
        rgb_to_xy(r, g, b, &x, &y);

        s_mirroring = true;
        ColorControl::Attributes::CurrentX::Set(endpoint_id, x);
        ColorControl::Attributes::CurrentY::Set(endpoint_id, y);
        ColorControl::Attributes::ColorMode::Set(endpoint_id,
            ColorControl::ColorModeEnum::kCurrentXAndCurrentY);
        ColorControl::Attributes::EnhancedColorMode::Set(endpoint_id,
            ColorControl::EnhancedColorModeEnum::kCurrentXAndCurrentY);
        s_mirroring = false;

        sync_zone_color(zone, endpoint_id);   // re-read: mode is XY now
        ESP_LOGI(TAG, "zone %d: normalised boot colour mode HS → XY", zone);
    } else {
        mirror_color_attributes(zone, endpoint_id);
    }
}

// ScheduleWork trampoline: attribute writes use the Accessors API and must run
// on the Matter thread; app_driver_light_set_defaults() runs in the app task.
static void mirror_work_handler(intptr_t arg)
{
    uint8_t  zone        = (uint8_t)((uint32_t)arg >> 16);
    uint16_t endpoint_id = (uint16_t)((uint32_t)arg & 0xFFFF);
    boot_normalize_color_mode(zone, endpoint_id);
}

// ---------- ColorControl command path hook ----------
//
// CHIP's ColorControl server processes MoveToHueAndSaturation /
// MoveToColor / MoveToColorTemperature commands internally and writes the
// resulting attributes via emberAfWriteAttribute().  That write path calls
// MatterPostAttributeChangeCallback (a weak/empty stub in esp-matter) but
// does NOT reliably fire the PRE_UPDATE callback registered via node::create().
//
// By providing a non-weak override here, we intercept every emberAfWriteAttribute
// call for the ColorControl cluster and re-sync the hardware state.
// This covers both the timer-based transition path from the ColorControl server
// and any direct WriteAttribute requests from the controller.

// Non-weak C++ override (matches the weak declaration in esp_matter_ember_stubs.cpp).
void MatterPostAttributeChangeCallback(const chip::app::ConcreteAttributePath &path,
                                       uint8_t type, uint16_t size, uint8_t *value)
{
    if (path.mClusterId != ColorControl::Id) return;

    // Ignore the writes generated by mirror_color_attributes() itself.
    if (s_mirroring) return;

    // During a colour-mode switch the server writes ColorMode/EnhancedColorMode
    // BEFORE the new mode's value attributes.  Driving the LEDs at that moment
    // briefly shows a stale colour (sub-second wrong-colour flash).  Skip here —
    // the value attribute that follows (hue/sat/XY/CT) triggers the update and
    // sync_zone_color() reads the fresh ColorMode then anyway.
    if (path.mAttributeId == ColorControl::Attributes::ColorMode::Id ||
        path.mAttributeId == ColorControl::Attributes::EnhancedColorMode::Id ||
        path.mAttributeId == ColorControl::Attributes::RemainingTime::Id) {
        return;
    }

    int8_t zone = endpoint_to_zone(path.mEndpointId);
    if (zone < 0) return;

    ESP_LOGI(TAG, "zone %d ColorControl attr=0x%04" PRIx32 " updated (server path)",
             (int)zone, path.mAttributeId);

    sync_zone_color((uint8_t)zone, path.mEndpointId);
    apply_zone_to_leds((uint8_t)zone);
    mirror_color_attributes((uint8_t)zone, path.mEndpointId);
}

// Read all light attributes from NVS-restored state and push to hardware at boot.
esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id)
{
    int8_t zone = endpoint_to_zone(endpoint_id);
    if (zone < 0) return ESP_ERR_NOT_FOUND;

    zone_state_t &z = s_zone_state[zone];
    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute_t *attr;

    // Brightness (LevelControl)
    attr = attribute::get(endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);
    if (attr && attribute::get_val(attr, &val) == ESP_OK)
        z.brightness = REMAP_TO_RANGE(val.val.u8, MATTER_BRIGHTNESS, STANDARD_BRIGHTNESS) / 100.0f;

    // OnOff state
    attr = attribute::get(endpoint_id, OnOff::Id, OnOff::Attributes::OnOff::Id);
    if (attr && attribute::get_val(attr, &val) == ESP_OK)
        z.power = val.val.b;

    // Full color state (ColorMode + hue/sat or CT or XY)
    sync_zone_color((uint8_t)zone, endpoint_id);

    apply_zone_to_leds((uint8_t)zone);

    // Mirror the colour into the other representations once at boot.  A zone
    // whose colour was never changed still has factory-default CurrentX/Y
    // (≈4000 K neutral white) alongside the red HS defaults — Apple Home
    // renders the slider from XY, showing light blue until the first colour
    // change ran the mirror.  Booting consistent removes that first-time state.
    CHIP_ERROR sched_err = chip::DeviceLayer::PlatformMgr().ScheduleWork(
        mirror_work_handler, ((intptr_t)(uint8_t)zone << 16) | endpoint_id);
    if (sched_err != CHIP_NO_ERROR) {
        ESP_LOGW(TAG, "zone %d: boot colour mirror not scheduled", zone);
    }

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
