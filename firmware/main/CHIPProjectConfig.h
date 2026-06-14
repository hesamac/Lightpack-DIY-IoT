#pragma once

// Project-level CHIP overrides, wired via CONFIG_CHIP_PROJECT_CONFIG.
//
// Sets the root-node Basic Information identity that CHIP serves from its
// compiled DeviceInstanceInfoProvider (not the writable data model). This is
// what appears in iOS Settings → General → Matter Accessories and in Apple
// Home's bridge accessory details.
//
// Note: the Vendor/Product *IDs* stay the Matter test IDs (0xFFF1 / 0x8000),
// so the device remains "uncertified" — only the display strings change.
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME  "Hesam DIY & Lightpack"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "Lightpack IOT"
