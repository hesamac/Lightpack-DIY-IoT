#pragma once
#include <esp_err.h>
#include <esp_matter.h>

// Matter attribute ranges
#define MATTER_BRIGHTNESS    254
#define MATTER_HUE           254
#define MATTER_SATURATION    254

// Standard working ranges used internally
#define STANDARD_BRIGHTNESS  100   // percent  (0–100)
#define STANDARD_HUE         360   // degrees  (0–360)
#define STANDARD_SATURATION  100   // percent  (0–100)

// Power-on defaults
#define DEFAULT_POWER        false
#define DEFAULT_BRIGHTNESS   64    // ~25 % of Matter range
#define DEFAULT_HUE          0
#define DEFAULT_SATURATION   254   // fully saturated

// Number of independently controllable LED zones
// Must match DM631_NUM_ZONES in dm631.h
#define NUM_LIGHT_ZONES      10

typedef void *app_driver_handle_t;

app_driver_handle_t app_driver_light_init(void);
app_driver_handle_t app_driver_button_init(void);

// Register the mapping between a Matter endpoint and a physical zone index (0–9).
// Must be called for each zone before esp_matter::start().
void app_driver_register_endpoint(uint8_t zone, uint16_t endpoint_id);

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle,
                                      uint16_t endpoint_id,
                                      uint32_t cluster_id,
                                      uint32_t attribute_id,
                                      esp_matter_attr_val_t *val);

// Reads NVS-persisted attributes for the given endpoint and pushes them to
// the corresponding DM631 zone. Call once per zone after esp_matter::start().
esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id);
