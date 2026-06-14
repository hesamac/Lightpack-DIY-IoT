#include <cstdio>
#include <cstring>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>
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

// One endpoint ID per LED zone (zones 0–9).
// Exported so app_driver.cpp can also reference them if needed.
uint16_t light_endpoint_ids[NUM_LIGHT_ZONES];

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
    ESP_LOGI(TAG, "Identify: ep=%u type=%u effect=%u variant=%u",
             endpoint_id, type, effect_id, effect_variant);
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

// Helper: apply the ColorCapabilities fix to a specific endpoint and enable
// Hue/Saturation feature. Must be called after the endpoint is created.
static void configure_color_endpoint(endpoint_t *ep, uint16_t endpoint_id)
{
    // ── Step 1: Create CurrentHue / CurrentSaturation attributes ─────────────
    //
    // extended_color_light::create() only adds XY attributes by default (the
    // Matter spec makes XY mandatory and HS optional for ExtendedColorLight).
    // hue_saturation::add() creates CurrentHue + CurrentSaturation + HS commands.
    //
    // Known issue: update_feature_map() inside hue_saturation::add() fails with
    // "Feature map attribute cannot be null" due to an ODR violation in the
    // esp-matter SDK (legacy esp_matter_feature.cpp and generated color_control.cpp
    // both export the same symbol; the legacy version can't find the FeatureMap
    // attribute created by the generated extended_color_light path).
    // The attribute creation succeeds anyway — only the FeatureMap update fails.
    // We fix the FeatureMap manually in step 3 below.
    {
        cluster_t *color_ctrl = esp_matter::cluster::get(ep, ColorControl::Id);
        esp_matter::cluster::color_control::feature::hue_saturation::config_t hs_cfg;
        hs_cfg.current_hue        = DEFAULT_HUE;
        hs_cfg.current_saturation = DEFAULT_SATURATION;
        esp_matter::cluster::color_control::feature::hue_saturation::add(color_ctrl, &hs_cfg);
    }

    // ── Step 2: ColorCapabilities = HS(bit0) + XY(bit3) = 0x0009 ─────────────
    //
    // EXPERIMENT (2026-06-14): drop ColorTemperature (bit4). HomeKit is widely
    // reported to mishandle lights that expose CT alongside HS/XY — it renders
    // the colour swatch as a default white/blue on a fresh read. Advertising
    // HS + XY only may make Apple treat it as a pure colour light and show the
    // real colour. (Revert with: git checkout firmware/main/app_main.cpp)
    {
        attribute_t *cc_attr = attribute::get(endpoint_id, ColorControl::Id,
                                              ColorControl::Attributes::ColorCapabilities::Id);
        if (cc_attr) {
            esp_matter_attr_val_t v = esp_matter_bitmap16(0x0009);
            attribute::set_val(cc_attr, &v);
            ESP_LOGI(TAG, "ep %d ColorCapabilities = 0x0009", endpoint_id);
        } else {
            ESP_LOGE(TAG, "ep %d: ColorCapabilities attribute not found!", endpoint_id);
        }
    }

    // ── Step 3: FeatureMap on ColorControl = HS + XY (CT bit cleared) ────────
    //
    //   bit 0 = HueSaturation    (0x0001)
    //   bit 3 = XY               (0x0008)
    //   bit 4 = ColorTemperature (0x0010)  ← cleared for this experiment
    //
    // extended_color_light::create() sets XY+CT, so we OR in HS and explicitly
    // clear the CT bit to land on 0x0009.
    // 0xFFFC = chip::app::Clusters::Globals::Attributes::FeatureMap::Id
    {
        attribute_t *fm_attr = attribute::get(endpoint_id, ColorControl::Id, 0xFFFC);
        if (fm_attr) {
            esp_matter_attr_val_t fm_val = esp_matter_invalid(NULL);
            attribute::get_val(fm_attr, &fm_val);
            uint32_t new_fm = (fm_val.val.u32 | 0x0009u) & ~0x0010u;   // HS+XY, clear CT
            esp_matter_attr_val_t v = esp_matter_bitmap32(new_fm);
            attribute::set_val(fm_attr, &v);
            ESP_LOGI(TAG, "ep %d ColorControl FeatureMap = 0x%08X", endpoint_id, (unsigned)new_fm);
        } else {
            ESP_LOGE(TAG, "ep %d: ColorControl FeatureMap not found!", endpoint_id);
        }
    }

    // ── Step 4: OnLevel + StartUpCurrentLevel = null ─────────────────────────
    //
    // A non-null OnLevel forces CurrentLevel to that value on EVERY OnOff
    // toggle-on (spec) — with the old OnLevel=64 each toggle snapped the zone
    // back to 25 % brightness.  Null = return to the previous brightness.
    // Likewise StartUpCurrentLevel null = restore last brightness at power-up.
    {
        attribute_t *ol_attr = attribute::get(endpoint_id, LevelControl::Id,
                                              LevelControl::Attributes::OnLevel::Id);
        if (ol_attr) {
            esp_matter_attr_val_t v = esp_matter_nullable_uint8(nullable<uint8_t>());
            attribute::set_val(ol_attr, &v);
        }
        attribute_t *sl_attr = attribute::get(endpoint_id, LevelControl::Id,
                                              LevelControl::Attributes::StartUpCurrentLevel::Id);
        if (sl_attr) {
            esp_matter_attr_val_t v = esp_matter_nullable_uint8(nullable<uint8_t>());
            attribute::set_val(sl_attr, &v);
        }
    }

    // ── Step 5: StartUpColorTemperatureMireds = null ─────────────────────────
    //
    // A non-null StartUpColorTemperatureMireds (the esp-matter default is 0,
    // which counts as non-null) forces ColorMode = ColorTemperature at every
    // power-up per the Matter spec.  The device then always booted as
    // "4000 K cool white": Apple Home primed its cache with that state and
    // rendered the slider light blue until the second colour change.
    // Null = restore the previous colour mode from NVS at boot instead.
    {
        attribute_t *su_attr = attribute::get(endpoint_id, ColorControl::Id,
                                              ColorControl::Attributes::StartUpColorTemperatureMireds::Id);
        if (su_attr) {
            esp_matter_attr_val_t v = esp_matter_nullable_uint16(nullable<uint16_t>());
            attribute::set_val(su_attr, &v);
            ESP_LOGI(TAG, "ep %d StartUpColorTemperatureMireds = null", endpoint_id);
        }
    }
}

extern "C" void app_main(void)
{
    nvs_flash_init();

    // Silence the CHIP stack's per-message INFO logging.  The Matter thread
    // logs kilobytes per command over the 115200-baud UART, blocking itself
    // for hundreds of ms — Apple Home shows that delay as a loading spinner
    // while it waits for the subscription report.  Errors/warnings still show;
    // our own app_driver/app_main logs stay at INFO.
    esp_log_level_set("chip[EM]",  ESP_LOG_WARN);
    esp_log_level_set("chip[IM]",  ESP_LOG_WARN);
    esp_log_level_set("chip[DMG]", ESP_LOG_WARN);
    esp_log_level_set("chip[ZCL]", ESP_LOG_WARN);
    esp_log_level_set("chip[DL]",  ESP_LOG_WARN);
    esp_log_level_set("chip[SC]",  ESP_LOG_WARN);
    esp_log_level_set("ROUTE_HOOK", ESP_LOG_WARN);
    esp_log_level_set("NimBLE",    ESP_LOG_WARN);

    app_driver_handle_t light_handle  = app_driver_light_init();
    app_driver_handle_t button_handle = app_driver_button_init();
    if (button_handle) {
        app_reset_button_register(button_handle);
    }

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    // -----------------------------------------------------------------------
    // Bridge topology:
    //   EP1            = Aggregator (the "bridge" itself)
    //   EP2 … EP11     = bridged Extended Colour Lights, one per LED zone.
    //
    // Presenting the zones as *bridged* devices (instead of bare endpoints on
    // one node) makes Apple Home treat each zone as a full standalone
    // accessory: its own tile with tap-to-toggle, its own room and name, and
    // independent use in scenes/automations.
    // -----------------------------------------------------------------------
    endpoint::aggregator::config_t aggregator_config;
    endpoint_t *aggregator_ep = endpoint::aggregator::create(node, &aggregator_config,
                                                             ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(aggregator_ep != nullptr,
                         ESP_LOGE(TAG, "Failed to create aggregator endpoint"));

    for (int zone = 0; zone < NUM_LIGHT_ZONES; zone++) {

        // Bridged-node endpoint: Descriptor + Bridged Device Basic Information.
        endpoint::bridged_node::config_t bridged_config;
        endpoint_t *ep = endpoint::bridged_node::create(node, &bridged_config,
                                                        ENDPOINT_FLAG_BRIDGE, light_handle);
        ABORT_APP_ON_FAILURE(ep != nullptr,
                             ESP_LOGE(TAG, "Failed to create bridged endpoint for zone %d", zone));
        endpoint::set_parent_endpoint(ep, aggregator_ep);

        // Per-tile identity shown in Apple Home (Bridged Device Basic Information).
        //   NodeLabel    → tile name ("LED N")
        //   VendorName   → Hersteller (Manufacturer)
        //   ProductName  → Modell (Model)
        //   SerialNumber → Seriennummer
        {
            char zone_name[16];
            snprintf(zone_name, sizeof(zone_name), "LED %d", zone + 1);
            char vendor[] = "Hesam DIY & Lightpack";
            char model[]  = "Lightpack IOT";
            char serial[] = "20202021";
            cluster_t *bdbi = cluster::get(ep, BridgedDeviceBasicInformation::Id);
            if (bdbi) {
                namespace bdbi_attr = esp_matter::cluster::bridged_device_basic_information::attribute;
                bdbi_attr::create_node_label(bdbi, zone_name, strlen(zone_name));
                bdbi_attr::create_vendor_name(bdbi, vendor, strlen(vendor));
                bdbi_attr::create_product_name(bdbi, model, strlen(model));
                bdbi_attr::create_serial_number(bdbi, serial, strlen(serial));
            }
        }

        // Add the Extended Colour Light device type + clusters to this endpoint.
        extended_color_light::config_t light_config;
        light_config.on_off.on_off                                    = DEFAULT_POWER;
        light_config.on_off_lighting.start_up_on_off                  = nullptr;
        light_config.level_control.current_level                      = DEFAULT_BRIGHTNESS;
        light_config.level_control.on_level                           = nullable<uint8_t>();
        light_config.level_control_lighting.start_up_current_level    = nullable<uint8_t>();
        // Default to HueSaturation mode — Apple Home controls colour with
        // MoveToHueAndSaturation, so HS is the native representation.
        light_config.color_control.color_mode          = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
        light_config.color_control.enhanced_color_mode = (uint8_t)ColorControl::ColorMode::kCurrentHueAndCurrentSaturation;
        // Advertise HS (bit 0) + XY (bit 3) — controls the colour wheel in Apple Home.
        light_config.color_control.color_capabilities  = 0x0009;

        esp_err_t light_err = extended_color_light::add(ep, &light_config);
        ABORT_APP_ON_FAILURE(light_err == ESP_OK,
                             ESP_LOGE(TAG, "Failed to add light device type for zone %d", zone));

        uint16_t ep_id = endpoint::get_id(ep);
        light_endpoint_ids[zone] = ep_id;

        // Register zone ↔ endpoint mapping in the driver before setup calls below.
        app_driver_register_endpoint((uint8_t)zone, ep_id);

        // Apply colour feature fix for this endpoint.
        configure_color_endpoint(ep, ep_id);

        // Mark rapidly-changing colour/brightness attributes for deferred NVS
        // persistence to reduce flash wear.
        // Guard each call: set_deferred_persistence logs an error if the
        // attribute is NULL (e.g. CurrentX/CurrentY not created in HS-only mode).
        {
            auto defer_if_exists = [&](uint32_t cluster_id, uint32_t attr_id) {
                attribute_t *a = attribute::get(ep_id, cluster_id, attr_id);
                if (a) attribute::set_deferred_persistence(a);
            };
            defer_if_exists(LevelControl::Id,  LevelControl::Attributes::CurrentLevel::Id);
            defer_if_exists(ColorControl::Id,  ColorControl::Attributes::CurrentHue::Id);
            defer_if_exists(ColorControl::Id,  ColorControl::Attributes::EnhancedCurrentHue::Id);
            defer_if_exists(ColorControl::Id,  ColorControl::Attributes::CurrentSaturation::Id);
            defer_if_exists(ColorControl::Id,  ColorControl::Attributes::CurrentX::Id);
            defer_if_exists(ColorControl::Id,  ColorControl::Attributes::CurrentY::Id);
            defer_if_exists(ColorControl::Id,  ColorControl::Attributes::ColorTemperatureMireds::Id);
        }

        ESP_LOGI(TAG, "Zone %d → endpoint id %d", zone, ep_id);
    }

    // Start the Matter stack.
    esp_err_t err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter: %d", err));

    // Disable Wi-Fi modem power save.  The default (WIFI_PS_MIN_MODEM) adds
    // 100–300 ms latency to every inbound packet, which Apple Home shows as a
    // loading spinner after each command.  This device is mains-powered, so
    // the extra ~60 mA idle draw is irrelevant; responsiveness matters more.
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Restore last colour/brightness from NVS and push to hardware for all zones.
    for (int zone = 0; zone < NUM_LIGHT_ZONES; zone++) {
        app_driver_light_set_defaults(light_endpoint_ids[zone]);
    }

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
