#include "ambilight_udp.h"
#include "output_controller.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "ambilight";

// Two protocols are accepted so different desktop tools work out of the box:
//   - WLED realtime (port 21324): DRGB / DNRGB. Used by WLED-native tools and
//     the simple broadcast test. HyperHDR's "WLED" device ALSO needs an HTTP
//     handshake we don't implement, so for HyperHDR use DDP instead.
//   - DDP (port 4048): Distributed Display Protocol. HyperHDR's "DDP" device
//     streams straight here with no handshake — the recommended path.
#define WLED_PORT        21324
#define DDP_PORT         4048
#define RECV_TIMEOUT_MS  500       // wake periodically to run output_tick()
#define RECV_BUF_SIZE    512       // 10 LEDs needs < 50 bytes; generous headroom

// WLED realtime selector (first byte)
#define WLED_DRGB        2         // [2][timeout][R,G,B ...]            from LED 0
#define WLED_DNRGB       4         // [4][timeout][startHi][startLo][RGB ...]

// 8-bit sRGB -> 12-bit DM631 with gamma 2.2, matching the Home colour path so a
// given colour looks the same whether it comes from Apple Home or Ambilight.
static inline uint16_t gamma8_to_12(uint8_t v)
{
    float f = v / 255.0f;
    f = powf(f, 2.2f);
    return (uint16_t)(f * 4095.0f);
}

// Copy `n` RGB triples from `p` into `frame`, starting at zone `start`.
static bool fill_frame(const uint8_t *p, int n, int start, dm631_color_t frame[])
{
    bool any = false;
    for (int i = 0; i < n; i++) {
        int zone = start + i;
        if (zone < 0 || zone >= DM631_NUM_ZONES) continue;
        frame[zone].r = gamma8_to_12(p[i * 3 + 0]);
        frame[zone].g = gamma8_to_12(p[i * 3 + 1]);
        frame[zone].b = gamma8_to_12(p[i * 3 + 2]);
        any = true;
    }
    return any;
}

// WLED realtime DRGB (proto 2) / DNRGB (proto 4). Returns true if frame updated.
static bool parse_wled(const uint8_t *buf, int len, dm631_color_t frame[])
{
    if (len < 2) return false;
    int offset, start;
    switch (buf[0]) {
    case WLED_DRGB:  offset = 2; start = 0; break;
    case WLED_DNRGB: if (len < 4) return false; offset = 4; start = (buf[2] << 8) | buf[3]; break;
    default:         return false;   // unsupported — ignore safely
    }
    return fill_frame(&buf[offset], (len - offset) / 3, start, frame);
}

// DDP (Distributed Display Protocol). Returns true if frame updated.
static bool parse_ddp(const uint8_t *buf, int len, dm631_color_t frame[])
{
    if (len < 10) return false;
    if ((buf[0] & 0xC0) != 0x40) return false;            // not DDP v1
    int header = (buf[0] & 0x10) ? 14 : 10;               // timecode present?
    if (len < header) return false;
    uint32_t data_off = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                        ((uint32_t)buf[6] << 8)  |  (uint32_t)buf[7];
    return fill_frame(&buf[header], (len - header) / 3, (int)(data_off / 3), frame);
}

static int make_udp_socket(uint16_t port)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return -1;
    struct sockaddr_in a = {};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port        = htons(port);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
        ESP_LOGE(TAG, "bind() failed on port %d", port);
        close(s);
        return -1;
    }
    return s;
}

static void ambilight_task(void *arg)
{
    static uint8_t buf[RECV_BUF_SIZE];
    dm631_color_t  frame[DM631_NUM_ZONES] = {};   // persists so partial updates accumulate

    int wled = make_udp_socket(WLED_PORT);
    int ddp  = make_udp_socket(DDP_PORT);
    if (wled < 0 && ddp < 0) {
        ESP_LOGE(TAG, "no sockets — ambilight disabled");
        vTaskDelete(NULL);
        return;
    }
    int maxfd = (wled > ddp ? wled : ddp) + 1;
    ESP_LOGI(TAG, "listening — WLED udp/%d, DDP udp/%d", WLED_PORT, DDP_PORT);

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        if (wled >= 0) FD_SET(wled, &rfds);
        if (ddp  >= 0) FD_SET(ddp,  &rfds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = RECV_TIMEOUT_MS * 1000 };

        int r = select(maxfd, &rfds, NULL, NULL, &tv);
        if (r <= 0) {
            output_tick();   // no frame this window — revert to Home if idle
            continue;
        }

        bool updated = false;
        if (wled >= 0 && FD_ISSET(wled, &rfds)) {
            int len = recvfrom(wled, buf, sizeof(buf), 0, NULL, NULL);
            if (len > 0 && parse_wled(buf, len, frame)) updated = true;
        }
        if (ddp >= 0 && FD_ISSET(ddp, &rfds)) {
            int len = recvfrom(ddp, buf, sizeof(buf), 0, NULL, NULL);
            if (len > 0 && parse_ddp(buf, len, frame)) updated = true;
        }
        if (updated) {
            output_set_ambilight_frame(frame);
        }
    }
}

void ambilight_udp_init(void)
{
    xTaskCreate(ambilight_task, "ambilight_udp", 4096, NULL, 5, NULL);
}
