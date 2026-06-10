/*
 * Descriptors.h — USB descriptor declarations for the Lightpack device.
 *
 * Device identity
 * ---------------
 *   VID  0x03EB  (ATMEL — widely used for DIY Lightpack builds)
 *   PID  0x2038  (Lightpack DIY)
 *
 * If Prismatik cannot find the device, check that its device list includes
 * these VID/PID values (Settings → Devices → Lightpack).
 */

#pragma once

#include <avr/pgmspace.h>
#include "LUFA/Drivers/USB/USB.h"

/* ---- USB identifiers ----------------------------------------- */
#define LIGHTPACK_VID      0x03EB
#define LIGHTPACK_PID      0x2038
#define LIGHTPACK_RELEASE  VERSION_BCD(5, 3, 0)   /* firmware v5.3 */

/* ---- Endpoint definitions ------------------------------------ */
#define HID_IN_EPADDR    (ENDPOINT_DIR_IN  | 1)   /* EP1 IN  — device → host */
#define HID_OUT_EPADDR   (ENDPOINT_DIR_OUT | 2)   /* EP2 OUT — host → device */
#define HID_EPSIZE       64                        /* bytes per report */

/* ---- USB string table indices -------------------------------- */
#define STRING_ID_Language      0
#define STRING_ID_Manufacturer  1
#define STRING_ID_Product       2
#define STRING_ID_Serial        3

/* ---- Configuration descriptor layout ------------------------- */
typedef struct {
    USB_Descriptor_Configuration_Header_t  Config;
    USB_Descriptor_Interface_t             HID_Interface;
    USB_HID_Descriptor_HID_t              HID_HID;
    USB_Descriptor_Endpoint_t             HID_ReportINEndpoint;
    USB_Descriptor_Endpoint_t             HID_ReportOUTEndpoint;
} USB_Descriptor_Configuration_t;

/* CALLBACK_USB_GetDescriptor is already declared by LUFA in Device.h.
 * Do NOT re-declare it here — doing so causes a "conflicting types" error.
 * The implementation lives in Descriptors.c. */
