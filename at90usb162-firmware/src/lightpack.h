/*
 * lightpack.h — Lightpack USB protocol (Prismatik-compatible)
 *
 * All commands are 64-byte HID OUT reports (no report ID byte).
 * Responses are 64-byte HID IN reports with the command byte echoed in byte 0.
 *
 * CMD_UPDATE_LEDS payload (bytes 1–30):
 *   10 LEDs × 3 bytes each: [R, G, B] as uint8 (0–255).
 *   The firmware scales each channel to 12-bit for the DM631 by left-shifting 4 bits
 *   (giving 0–4080) and then multiplying by the brightness factor.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ---- Report size --------------------------------------------- */
#define LIGHTPACK_REPORT_SIZE  64

/* ---- USB protocol commands ----------------------------------- */
#define CMD_UPDATE_LEDS           1   /* host → device: set all zone colours   */
#define CMD_OFF_ALL               2   /* host → device: blank all zones        */
#define CMD_SET_TIMER_OPTIONS     3   /* reserved                              */
#define CMD_SET_SMOOTH_SLOWDOWN   4   /* reserved (smoothing not implemented)  */
#define CMD_SET_BRIGHTNESS        5   /* host → device: buf[1] = 0–100 %      */
#define CMD_PING                  6   /* host → device: keepalive              */
#define CMD_GET_STATUS            7   /* host → device: query status           */
#define CMD_GET_FIRMWARE_VERSION  8   /* host → device: returns major, minor   */
#define CMD_GET_SERIAL            9   /* host → device: returns ASCII serial   */
#define CMD_LOCK_DEVICE          13   /* host → device: exclusive lock         */
#define CMD_UNLOCK_DEVICE        14   /* host → device: release lock           */
#define CMD_NOT_BUSY_UPDATING    15   /* host → device: signal idle            */

/* ---- Response / status byte (buf[1] in every response) ------- */
#define STATUS_OK              0
#define STATUS_BUSY            1
#define STATUS_IDLE            2
#define STATUS_DEVICE_LOCKED   3
#define STATUS_UNKNOWN_CMD     4

/* ---- Firmware version reported to host ----------------------- */
#define FW_VERSION_MAJOR       5
#define FW_VERSION_MINOR       3

/* ---- API ----------------------------------------------------- */

/** Initialise the protocol layer (calls dm631_init). */
void lightpack_init(void);

/**
 * Process one 64-byte OUT report and fill a 64-byte IN response.
 *
 * @param report   raw HID report bytes (must be LIGHTPACK_REPORT_SIZE bytes)
 * @param response output buffer for the IN report  (LIGHTPACK_REPORT_SIZE bytes)
 */
void lightpack_process(const uint8_t *report, uint8_t *response);
