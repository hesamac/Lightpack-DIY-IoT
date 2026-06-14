#include "ambilight_udp.h"
#include "output_controller.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "ambilight";

#define AMBILIGHT_UDP_PORT  21324    // WLED realtime UDP port
#define RECV_TIMEOUT_MS     500      // wake periodically to run output_tick()
#define RECV_BUF_SIZE       512      // 10 LEDs needs ~34 bytes; generous headroom

// WLED realtime protocol selector (first byte):
#define WLED_DRGB           2        // [2][timeout][R,G,B ...]            from LED 0
#define WLED_DNRGB          4        // [4][timeout][startHi][startLo][R,G,B ...]

// 8-bit sRGB -> 12-bit DM631 with gamma 2.2, matching the Home colour path so a
// given colour looks the same whether it comes from Apple Home or Ambilight.
static inline uint16_t gamma8_to_12(uint8_t v)
{
    float f = v / 255.0f;
    f = powf(f, 2.2f);
    return (uint16_t)(f * 4095.0f);
}

static void ambilight_task(void *arg)
{
    static uint8_t buf[RECV_BUF_SIZE];
    // Persists across packets so partial DNRGB updates accumulate.
    dm631_color_t frame[DM631_NUM_ZONES] = {};

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(AMBILIGHT_UDP_PORT);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed on port %d", AMBILIGHT_UDP_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    struct timeval tv = { .tv_sec = 0, .tv_usec = RECV_TIMEOUT_MS * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "listening on UDP %d (WLED DRGB/DNRGB)", AMBILIGHT_UDP_PORT);

    while (1) {
        int len = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
        if (len < 0) {
            // Receive timeout: no frame this window — let the output layer
            // revert to Home mode if Ambilight has gone idle.
            output_tick();
            continue;
        }
        if (len < 2) {
            continue;   // runt packet
        }

        int offset, start;
        switch (buf[0]) {
        case WLED_DRGB:
            offset = 2; start = 0;
            break;
        case WLED_DNRGB:
            if (len < 4) continue;
            offset = 4; start = (buf[1 + 1] << 8) | buf[1 + 2];   // buf[2..3]
            break;
        default:
            continue;   // unsupported protocol — ignore safely
        }

        int n = (len - offset) / 3;   // RGB triples in this packet
        if (n <= 0) {
            continue;
        }

        for (int i = 0; i < n; i++) {
            int zone = start + i;
            if (zone < 0 || zone >= DM631_NUM_ZONES) {
                continue;   // ignore LEDs outside our 10 zones
            }
            const uint8_t *p = &buf[offset + i * 3];
            frame[zone].r = gamma8_to_12(p[0]);
            frame[zone].g = gamma8_to_12(p[1]);
            frame[zone].b = gamma8_to_12(p[2]);
        }

        output_set_ambilight_frame(frame);
    }
}

void ambilight_udp_init(void)
{
    xTaskCreate(ambilight_task, "ambilight_udp", 4096, NULL, 5, NULL);
}
