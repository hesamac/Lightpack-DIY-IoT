/*
 * main.c — Lightpack firmware for AT90USB162
 *
 * Architecture
 * ------------
 *  - LUFA USB stack (full-speed USB 1.1 device).
 *  - USB HID class with a 64-byte IN endpoint and a 64-byte OUT endpoint.
 *  - All USB management runs in the USB ISR (INTERRUPT_CONTROL_ENDPOINT).
 *  - Main loop polls the OUT endpoint for incoming Prismatik commands,
 *    calls lightpack_process(), then sends the response on the IN endpoint.
 *  - DM631 LED driver is bit-bang SPI; dm631_update() is called from
 *    lightpack_process() inside CMD_UPDATE_LEDS and CMD_OFF_ALL.
 *
 * USB event flow
 * --------------
 *  Host connects → EVENT_USB_Device_ConfigurationChanged → endpoints opened
 *  Host sends OUT report → main loop reads it → lightpack_process()
 *  Main loop sends IN  report with status/response
 *
 * Flashing
 * --------
 *  Press the HWB button on the Lightpack board to enter DFU bootloader, then:
 *      dfu-programmer at90usb162 erase
 *      dfu-programmer at90usb162 flash lightpack.hex
 *      dfu-programmer at90usb162 launch
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <string.h>

#include "LUFA/Drivers/USB/USB.h"
#include "Descriptors.h"
#include "lightpack.h"
#include "dm631.h"

/* ---- Module buffers ------------------------------------------ */

/* OUT buffer: one report received from the host. */
static uint8_t out_buf[HID_EPSIZE];

/* IN buffer: one report to be sent to the host. */
static uint8_t in_buf[HID_EPSIZE];

/* ----------------------------------------------------------------
 * USB Event Handlers  (called by LUFA from the USB ISR)
 * -------------------------------------------------------------- */

/** Called when the USB host successfully configures the device. */
void EVENT_USB_Device_ConfigurationChanged(void)
{
    /* Open interrupt IN endpoint (device → host, responses). */
    Endpoint_ConfigureEndpoint(HID_IN_EPADDR,
                               EP_TYPE_INTERRUPT,
                               HID_EPSIZE,
                               1);

    /* Open interrupt OUT endpoint (host → device, commands). */
    Endpoint_ConfigureEndpoint(HID_OUT_EPADDR,
                               EP_TYPE_INTERRUPT,
                               HID_EPSIZE,
                               1);
}

/** Called for every USB control request the stack does not handle itself. */
void EVENT_USB_Device_ControlRequest(void)
{
    switch (USB_ControlRequest.bRequest) {

    /* HID GET_REPORT via control endpoint — host is polling for IN data.
     * Return a simple STATUS_OK ping response. */
    case HID_REQ_GetReport:
        if (USB_ControlRequest.bmRequestType ==
            (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
        {
            memset(in_buf, 0, HID_EPSIZE);
            in_buf[0] = CMD_PING;
            in_buf[1] = STATUS_OK;

            Endpoint_ClearSETUP();
            Endpoint_Write_Control_Stream_LE(in_buf, HID_EPSIZE);
            Endpoint_ClearOUT();
        }
        break;

    /* HID SET_REPORT via control endpoint — host sent an OUT report on EP0. */
    case HID_REQ_SetReport:
        if (USB_ControlRequest.bmRequestType ==
            (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
        {
            Endpoint_ClearSETUP();
            Endpoint_Read_Control_Stream_LE(out_buf, HID_EPSIZE);
            Endpoint_ClearIN();

            lightpack_process(out_buf, in_buf);
            /* Response is queued for next IN poll — no immediate reply on EP0. */
        }
        break;
    }
}

/** Called when the USB cable is unplugged. */
void EVENT_USB_Device_Disconnect(void)
{
    /* Blank all LEDs so the board does not stay lit when USB is removed. */
    dm631_off_all();
}

/* ----------------------------------------------------------------
 * Main
 * -------------------------------------------------------------- */

int main(void)
{
    /* ----- Initialise MCU ------------------------------------ */

    /* Disable watchdog timer (it can be set by the bootloader). */
    MCUSR &= ~(1 << WDRF);
    wdt_disable();

    /* Remove clock prescaler — run at full 16 MHz. */
    clock_prescale_set(clock_div_1);

    /* ----- Initialise peripherals ---------------------------- */

    /* Blank all DM631 LEDs. */
    lightpack_init();

    /* Prepare a default STATUS_IDLE response so the IN endpoint is never empty. */
    memset(in_buf, 0, HID_EPSIZE);
    in_buf[0] = CMD_GET_STATUS;
    in_buf[1] = STATUS_IDLE;

    /* ----- Initialise USB stack ------------------------------- */
    USB_Init();

    /* ----- Enable interrupts ---------------------------------- */
    sei();

    /* ----- Main loop ------------------------------------------ */
    for (;;) {
        /* Must be called every iteration in polled USB mode.
         * Handles setup packets, state transitions, and control requests. */
        USB_USBTask();

        /* Only touch data endpoints when the host has configured the device. */
        if (USB_DeviceState != DEVICE_STATE_Configured)
            continue;

        /* ---- Receive commands (OUT endpoint) ---------------- */
        Endpoint_SelectEndpoint(HID_OUT_EPADDR);

        if (Endpoint_IsOUTReceived()) {
            /* Read the 64-byte report from the OUT FIFO. */
            Endpoint_Read_Stream_LE(out_buf, sizeof(out_buf), NULL);
            Endpoint_ClearOUT();

            /* Process the command and fill in_buf with the response. */
            lightpack_process(out_buf, in_buf);
        }

        /* ---- Send response (IN endpoint) -------------------- */
        Endpoint_SelectEndpoint(HID_IN_EPADDR);

        if (Endpoint_IsINReady()) {
            /* Push in_buf to the IN FIFO.
             * The host polls this endpoint every 1 ms; the response will be
             * read on the next poll.  We re-send in_buf every time the IN
             * endpoint is ready — Prismatik discards duplicate status packets. */
            Endpoint_Write_Stream_LE(in_buf, sizeof(in_buf), NULL);
            Endpoint_ClearIN();
        }
    }
}
