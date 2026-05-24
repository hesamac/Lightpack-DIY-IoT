#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>
#include <common_macros.h>
#include <app_priv.h>
#include <app_reset.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

static const char *TAG = "app_main";
uint16_t light_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

static constexpr auto k_timeout_seconds = 300;

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "IP address changed");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed — fail-safe expired");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Fabric removed");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            if (!mgr.IsCommissioningWindowOpen()) {
                (void)mgr.OpenBasicCommissioningWindow(
                    chip::System::Clock::Seconds16(k_timeout_seconds),
                    chip::CommissioningWindowAdvertisement::kAllSupported);
            }
        }
        break;
    }
    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized — memory reclaimed");
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type,
                                       uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identify: type=%u effect=%u variant=%u", type, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type,
                                         uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id,
                                         esp_matter_attr_val_t *val, void *priv_data)
{
    if (type == PRE_UPDATE) {
        return app_driver_attribute_update((app_driver_handle_t)priv_data,
                                           endpoint_id, cluster_id, attribute_id, val);
    }
    return ESP_OK;
}

extern "C" void app_main(void)
{
    nvs_flash_init();

    app_driver_handle_t light_handle  = app_driver_light_init();
    app_driver_handle_t button_handle = app_driver_button_init();
    if (button_handle) {
        app_reset_button_register(button_handle);
    }

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    // Extended colour light: supports On/Off, Level, Hue/Sat, XY
    extended_color_light::config_t light_config;
    light_config.on_off.on_off                                    = DEFAULT_POWER;
    light_config.on_off_lighting.start_up_on_off                  = nullptr;
    light_config.level_control.current_level                      = DEFAULT_BRIGHTNESS;
    light_config.level_control.on_level                           = DEFAULT_BRIGHTNESS;
    light_config.level_control_lighting.start_up_current_level    = DEFAULT_BRIGHTNESS;
    light_config.color_control.color_mode          = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    light_config.color_control.enhanced_color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
    // Advertise HS (bit 0) + XY (bit 3) — controls the color wheel in Apple Home.
    // feature::add() only updates FeatureMap, not this attribute.
    light_config.color_control.color_capabilities  = 0x0009;

    endpoint_t *endpoint = extended_color_light::create(node, &light_config,
                                                   ENDPOINT_FLAG_NONE, light_handle);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create light endpoint"));

    light_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Light endpoint id: %d", light_endpoint_id);

    // Hue/Saturation is optional in the spec and not added by extended_color_light::create().
    // Adding it here is the correct fix per esp-matter Issue #963.
    {
        cluster_t *color_ctrl = esp_matter::cluster::get(endpoint, ColorControl::Id);
        esp_matter::cluster::color_control::feature::hue_saturation::config_t hs_cfg;
        hs_cfg.current_hue        = DEFAULT_HUE;
        hs_cfg.current_saturation = DEFAULT_SATURATION;
        esp_matter::cluster::color_control::feature::hue_saturation::add(color_ctrl, &hs_cfg);
    }

    // Force ColorCapabilities = HS(bit0) + XY(bit3) = 0x0009.
    // The config field assignment in app_main is silently lost when the linker picks the
    // legacy color_control::create() over the generated one (ODR violation); the two
    // config_t structs have color_capabilities at different byte offsets, so the legacy
    // code reads 0 from a primary_x field. Setting it directly on the attribute object
    // after endpoint creation is the only reliable way to guarantee the correct value.
    {
        attribute_t *cc_attr = attribute::get(light_endpoint_id, ColorControl::Id,
                                              ColorControl::Attributes::ColorCapabilities::Id);
        if (cc_attr) {
            esp_matter_attr_val_t v = esp_matter_bitmap16(0x0009);
            attribute::set_val(cc_attr, &v);
            esp_matter_attr_val_t chk = esp_matter_invalid(NULL);
            attribute::get_val(cc_attr, &chk);
            ESP_LOGI(TAG, "ColorCapabilities = 0x%04X (expect 0x0009)", chk.val.u16);
        } else {
            ESP_LOGE(TAG, "ColorCapabilities attribute not found!");
        }
    }

    // Mark rapidly-changing attributes for deferred NVS persistence to reduce flash wear
    attribute::set_deferred_persistence(
        attribute::get(light_endpoint_id, LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id));
    attribute::set_deferred_persistence(
        attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentX::Id));
    attribute::set_deferred_persistence(
        attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentY::Id));
    attribute::set_deferred_persistence(
        attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentHue::Id));
    attribute::set_deferred_persistence(
        attribute::get(light_endpoint_id, ColorControl::Id, ColorControl::Attributes::CurrentSaturation::Id));

    esp_err_t err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter: %d", err));

    // Restore last colour/brightness from NVS and push to hardware
    app_driver_light_set_defaults(light_endpoint_id);

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
    esp_matter::console::init();
#endif

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
