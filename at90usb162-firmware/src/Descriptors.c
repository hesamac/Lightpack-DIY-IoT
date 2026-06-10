/*
 * Descriptors.c — USB descriptors for the Lightpack AT90USB162 firmware.
 *
 * Implements the LUFA callback CALLBACK_USB_GetDescriptor which is called by
 * the USB stack whenever the host requests a descriptor.
 *
 * HID Report Descriptor
 * ---------------------
 * Vendor-defined usage page.  One 64-byte IN report (device → host)
 * and one 64-byte OUT report (host → device).  No report IDs are used;
 * the first byte of each report is the Lightpack command / status byte.
 */

#include "Descriptors.h"

/* ---- HID Report Descriptor ----------------------------------- */

const USB_Descriptor_HIDReport_Datatype_t PROGMEM HIDReport[] = {
    HID_RI_USAGE_PAGE(8,  0xFF),        /* Vendor-defined usage page           */
    HID_RI_USAGE(8,       0x01),        /* Vendor usage 1                      */
    HID_RI_COLLECTION(8,  0x01),        /* Application collection              */
        HID_RI_LOGICAL_MINIMUM(8, 0x00),
        HID_RI_LOGICAL_MAXIMUM(16, 0x00FF), /* 0–255, explicit 16-bit avoids sign */
        HID_RI_REPORT_SIZE(8,   8),     /* 8 bits per field                    */
        HID_RI_REPORT_COUNT(8, 64),     /* 64 fields = 64-byte report          */
        HID_RI_USAGE(8, 0x01),
        HID_RI_INPUT(8,  HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
        HID_RI_REPORT_COUNT(8, 64),
        HID_RI_USAGE(8, 0x01),
        HID_RI_OUTPUT(8, HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE
                       | HID_IOF_NON_VOLATILE),
    HID_RI_END_COLLECTION(0),
};

/* ---- Device Descriptor --------------------------------------- */

const USB_Descriptor_Device_t PROGMEM DeviceDescriptor = {
    .Header = {
        .Size = sizeof(USB_Descriptor_Device_t),
        .Type = DTYPE_Device
    },
    .USBSpecification       = VERSION_BCD(1, 1, 0),
    .Class                  = USB_CSCP_NoDeviceClass,
    .SubClass               = USB_CSCP_NoDeviceSubclass,
    .Protocol               = USB_CSCP_NoDeviceProtocol,
    .Endpoint0Size          = FIXED_CONTROL_ENDPOINT_SIZE,
    .VendorID               = LIGHTPACK_VID,
    .ProductID              = LIGHTPACK_PID,
    .ReleaseNumber          = LIGHTPACK_RELEASE,
    .ManufacturerStrIndex   = STRING_ID_Manufacturer,
    .ProductStrIndex        = STRING_ID_Product,
    .SerialNumStrIndex      = STRING_ID_Serial,
    .NumberOfConfigurations = FIXED_NUM_CONFIGURATIONS,
};

/* ---- Configuration + Interface + HID + Endpoint Descriptors -- */

const USB_Descriptor_Configuration_t PROGMEM ConfigurationDescriptor = {
    .Config = {
        .Header = {
            .Size = sizeof(USB_Descriptor_Configuration_Header_t),
            .Type = DTYPE_Configuration
        },
        .TotalConfigurationSize = sizeof(USB_Descriptor_Configuration_t),
        .TotalInterfaces        = 1,
        .ConfigurationNumber    = 1,
        .ConfigurationStrIndex  = NO_DESCRIPTOR,
        .ConfigAttributes       = USB_CONFIG_ATTR_RESERVED,
        .MaxPowerConsumption    = USB_CONFIG_POWER_MA(100),
    },

    .HID_Interface = {
        .Header = {
            .Size = sizeof(USB_Descriptor_Interface_t),
            .Type = DTYPE_Interface
        },
        .InterfaceNumber    = 0,
        .AlternateSetting   = 0,
        .TotalEndpoints     = 2,
        .Class              = HID_CSCP_HIDClass,
        .SubClass           = HID_CSCP_NonBootSubclass,
        .Protocol           = HID_CSCP_NonBootProtocol,
        .InterfaceStrIndex  = NO_DESCRIPTOR,
    },

    .HID_HID = {
        .Header = {
            .Size = sizeof(USB_HID_Descriptor_HID_t),
            .Type = HID_DTYPE_HID
        },
        .HIDSpec                = VERSION_BCD(1, 1, 1),
        .CountryCode            = 0,
        .TotalReportDescriptors = 1,
        .HIDReportType          = HID_DTYPE_Report,
        .HIDReportLength        = sizeof(HIDReport),
    },

    /* EP1 IN — device → host (status / ping responses) */
    .HID_ReportINEndpoint = {
        .Header = {
            .Size = sizeof(USB_Descriptor_Endpoint_t),
            .Type = DTYPE_Endpoint
        },
        .EndpointAddress    = HID_IN_EPADDR,
        .Attributes         = (EP_TYPE_INTERRUPT | ENDPOINT_ATTR_NO_SYNC
                               | ENDPOINT_USAGE_DATA),
        .EndpointSize       = HID_EPSIZE,
        .PollingIntervalMS  = 1,
    },

    /* EP2 OUT — host → device (commands / colour data) */
    .HID_ReportOUTEndpoint = {
        .Header = {
            .Size = sizeof(USB_Descriptor_Endpoint_t),
            .Type = DTYPE_Endpoint
        },
        .EndpointAddress    = HID_OUT_EPADDR,
        .Attributes         = (EP_TYPE_INTERRUPT | ENDPOINT_ATTR_NO_SYNC
                               | ENDPOINT_USAGE_DATA),
        .EndpointSize       = HID_EPSIZE,
        .PollingIntervalMS  = 1,
    },
};

/* ---- String Descriptors -------------------------------------- */

/* Language: English (US). */
const USB_Descriptor_String_t PROGMEM LanguageString =
    USB_STRING_DESCRIPTOR_ARRAY(LANGUAGE_ID_ENG);

/* Manufacturer. */
const USB_Descriptor_String_t PROGMEM ManufacturerString =
    USB_STRING_DESCRIPTOR(L"Lightpack DIY");

/* Product name. */
const USB_Descriptor_String_t PROGMEM ProductString =
    USB_STRING_DESCRIPTOR(L"Lightpack");

/* Serial number (shown in OS device manager). */
const USB_Descriptor_String_t PROGMEM SerialString =
    USB_STRING_DESCRIPTOR(L"LPK-001");

/* ---- CALLBACK_USB_GetDescriptor ------------------------------ */

uint16_t CALLBACK_USB_GetDescriptor(const uint16_t wValue,
                                    const uint16_t wIndex,
                                    const void **const DescriptorAddress)
{
    const uint8_t DescType   = (wValue >> 8);
    const uint8_t DescNumber = (wValue & 0xFF);

    const void *Address = NULL;
    uint16_t    Size    = NO_DESCRIPTOR;

    (void)wIndex;   /* unused; single interface, no alternate settings */

    switch (DescType) {

    case DTYPE_Device:
        Address = &DeviceDescriptor;
        Size    = sizeof(USB_Descriptor_Device_t);
        break;

    case DTYPE_Configuration:
        Address = &ConfigurationDescriptor;
        Size    = sizeof(USB_Descriptor_Configuration_t);
        break;

    case DTYPE_String:
        switch (DescNumber) {
        case STRING_ID_Language:
            Address = &LanguageString;
            Size    = pgm_read_byte(&LanguageString.Header.Size);
            break;
        case STRING_ID_Manufacturer:
            Address = &ManufacturerString;
            Size    = pgm_read_byte(&ManufacturerString.Header.Size);
            break;
        case STRING_ID_Product:
            Address = &ProductString;
            Size    = pgm_read_byte(&ProductString.Header.Size);
            break;
        case STRING_ID_Serial:
            Address = &SerialString;
            Size    = pgm_read_byte(&SerialString.Header.Size);
            break;
        }
        break;

    case HID_DTYPE_Report:
        Address = &HIDReport;
        Size    = sizeof(HIDReport);
        break;
    }

    *DescriptorAddress = Address;
    return Size;
}
