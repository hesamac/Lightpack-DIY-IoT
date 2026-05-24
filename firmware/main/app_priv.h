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
#define DEFAULT_POWER        true
#define DEFAULT_BRIGHTNESS   64    // ~25 % of Matter range
#define DEFAULT_HUE          0
#define DEFAULT_SATURATION   254   // fully saturated

typedef void *app_driver_handle_t;

app_driver_handle_t app_driver_light_init(void);
app_driver_handle_t app_driver_button_init(void);

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle,
                                      uint16_t endpoint_id,
                                      uint32_t cluster_id,
                                      uint32_t attribute_id,
                                      esp_matter_attr_val_t *val);

esp_err_t app_driver_light_set_defaults(uint16_t endpoint_id);
