/*
 * lightpack.c — Prismatik-compatible USB protocol handler
 *
 * Protocol summary
 * ----------------
 * Both directions use flat 64-byte HID reports (no report-ID byte).
 *
 * Host → Device (OUT report):
 *   byte[0]  = command (CMD_*)
 *   byte[1…] = command-specific payload
 *
 * Device → Host (IN report):
 *   byte[0]  = echoed command byte
 *   byte[1]  = STATUS_* code
 *   byte[2…] = optional response data
 *
 * CMD_UPDATE_LEDS payload layout (bytes 1–30):
 *   LED 0:  byte[1]=R  byte[2]=G  byte[3]=B   (uint8, 0–255)
 *   LED 1:  byte[4]=R  byte[5]=G  byte[6]=B
 *   …
 *   LED 9:  byte[28]=R byte[29]=G byte[30]=B
 *
 * R/G/B are scaled: dm631_12bit = uint8_value << 4  (0–255 → 0–4080).
 * Brightness then scales the 12-bit value further: val = val * brightness / 100.
 */

#include "lightpack.h"
#include "dm631.h"
#include <string.h>
#include <avr/pgmspace.h>

/* Serial string returned by CMD_GET_SERIAL (fits in flash). */
static const char serial_str[] PROGMEM = "LPK-001";

/* ---- Device state -------------------------------------------- */
static bool    s_locked     = false;
static uint8_t s_brightness = 100;   /* 0–100 % */

/* ---- Helpers ------------------------------------------------- */

/**
 * Scale an 8-bit channel value to 12-bit with brightness applied.
 *   Step 1: shift left 4  → 0–255 becomes 0–4080  (≈12-bit, missing 15 counts at top)
 *   Step 2: multiply by brightness / 100
 */
static uint16_t scale(uint8_t val8)
{
    uint16_t val12 = (uint16_t)val8 << 4;   /* 0–4080 */
    if (s_brightness == 100)
        return val12;
    return (uint16_t)((uint32_t)val12 * s_brightness / 100);
}

/* ---- Public API --------------------------------------------- */

void lightpack_init(void)
{
    dm631_init();
}

void lightpack_process(const uint8_t *buf, uint8_t *resp)
{
    uint8_t cmd = buf[0];

    memset(resp, 0, LIGHTPACK_REPORT_SIZE);
    resp[0] = cmd;          /* echo command */
    resp[1] = STATUS_OK;    /* default: success */

    switch (cmd) {

    /* ---- CMD_UPDATE_LEDS (1) ---------------------------------- */
    case CMD_UPDATE_LEDS:
        if (s_locked) {
            resp[1] = STATUS_DEVICE_LOCKED;
            break;
        }
        for (uint8_t i = 0; i < DM631_NUM_ZONES; i++) {
            const uint8_t *p = &buf[1 + i * 3];
            dm631_color_t c = {
                .r = scale(p[0]),
                .g = scale(p[1]),
                .b = scale(p[2]),
            };
            dm631_set_zone(i, c);
        }
        dm631_update();
        break;

    /* ---- CMD_OFF_ALL (2) -------------------------------------- */
    case CMD_OFF_ALL:
        dm631_off_all();
        break;

    /* ---- CMD_SET_BRIGHTNESS (5) ------------------------------- */
    /* buf[1] = brightness 0–100 */
    case CMD_SET_BRIGHTNESS:
        s_brightness = (buf[1] > 100) ? 100 : buf[1];
        break;

    /* ---- CMD_PING (6) ---------------------------------------- */
    case CMD_PING:
        /* STATUS_OK already set */
        break;

    /* ---- CMD_GET_STATUS (7) ----------------------------------- */
    case CMD_GET_STATUS:
        resp[1] = s_locked ? STATUS_DEVICE_LOCKED : STATUS_IDLE;
        break;

    /* ---- CMD_GET_FIRMWARE_VERSION (8) ------------------------- */
    case CMD_GET_FIRMWARE_VERSION:
        resp[1] = FW_VERSION_MAJOR;
        resp[2] = FW_VERSION_MINOR;
        break;

    /* ---- CMD_GET_SERIAL (9) ----------------------------------- */
    case CMD_GET_SERIAL: {
        uint8_t len = (uint8_t)strlen_P(serial_str);
        for (uint8_t i = 0; i < len && (i + 2) < LIGHTPACK_REPORT_SIZE; i++)
            resp[2 + i] = pgm_read_byte(&serial_str[i]);
        break;
    }

    /* ---- CMD_LOCK_DEVICE (13) --------------------------------- */
    case CMD_LOCK_DEVICE:
        s_locked = true;
        break;

    /* ---- CMD_UNLOCK_DEVICE (14) ------------------------------- */
    case CMD_UNLOCK_DEVICE:
        s_locked = false;
        break;

    /* ---- CMD_NOT_BUSY_UPDATING (15) --------------------------- */
    case CMD_NOT_BUSY_UPDATING:
        /* Prismatik sends this after a CMD_UPDATE_LEDS burst; no action needed. */
        break;

    default:
        resp[1] = STATUS_UNKNOWN_CMD;
        break;
    }
}
