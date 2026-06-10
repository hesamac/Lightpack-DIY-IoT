/*
 * LUFAConfig.h — Compile-time configuration for the LUFA USB library.
 *
 * Included by LUFA when USE_LUFA_CONFIG_HEADER is defined (set in the Makefile).
 * See LUFA/Common/Common.h and LUFA/Drivers/USB/USB.h for option descriptions.
 */

#pragma once

/* ---- Architecture -------------------------------------------- */
#define ARCH                    ARCH_AVR8

/* ---- USB controller ------------------------------------------ */
/* Full-speed only device, internal regulator enabled, auto PLL.   */
#define USE_STATIC_OPTIONS      (USB_DEVICE_OPT_FULLSPEED   \
                                | USB_OPT_REG_ENABLED        \
                                | USB_OPT_AUTO_PLL)

/* Only build the Device-mode USB stack (saves ~1 KB flash). */
#define USB_DEVICE_ONLY

/* ---- Endpoints ----------------------------------------------- */
/* Control endpoint packet size (max for AT90USB162 is 8 bytes).   */
#define FIXED_CONTROL_ENDPOINT_SIZE  8

/* Only one USB configuration supported. */
#define FIXED_NUM_CONFIGURATIONS     1

/* ---- Descriptor storage location ----------------------------- */
/* All descriptors are stored in flash (PROGMEM).
 * This does two things:
 *   1. Tells LUFA to use pgm_read_byte() when copying descriptor data.
 *   2. Removes the 4th "DescriptorMemorySpace" parameter from the
 *      CALLBACK_USB_GetDescriptor() prototype (the extra parameter only
 *      appears on multi-address-space architectures when no storage type
 *      is explicitly specified). */
#define USE_FLASH_DESCRIPTORS

/* ---- AT90USB162 suspend / connect quirk ---------------------- */
/* The AT90USB162 belongs to USB_SERIES_2_AVR and cannot detect VBUS
 * directly.  By default LUFA treats a suspend event (no SOF packets
 * for 3 ms) as a cable disconnect, setting DeviceState back to
 * DEVICE_STATE_Unattached.  macOS pauses SOF packets briefly during
 * initial enumeration, which fires the suspend interrupt and silently
 * kills enumeration before the device ever appears in system_profiler.
 *
 * NO_LIMITED_CONTROLLER_CONNECT disables that behaviour: a suspend
 * sets DeviceState to DEVICE_STATE_Suspended (normal), and the device
 * continues enumerating correctly.  The trade-off — physically
 * unplugging the cable won't fire EVENT_USB_Device_Disconnect — is
 * acceptable for a stationary LED driver. */
#define NO_LIMITED_CONTROLLER_CONNECT

/* ---- Polled USB mode ----------------------------------------- */
/* USB_USBTask() is called in the main loop (see main.c).
 * INTERRUPT_CONTROL_ENDPOINT is intentionally NOT defined:
 *   - Interrupt mode relies on USB_COM_vect firing before global
 *     interrupts are enabled, which can silently fail on some
 *     bootloader/toolchain combinations.
 *   - Polled mode is simpler, reliable, and used by all LUFA demos.
 * DEVICE_STATE_AS_GPIOR is also NOT defined: storing device state in
 * GPIOR0 can conflict with LUFA internals and prevent enumeration. */
