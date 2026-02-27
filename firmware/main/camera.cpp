#include "camera.h"
#include "config.h"
#include "esp_camera.h"
#include "esp_log.h"

static const char *TAG = "CAMERA";

bool camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn     = PWDN_GPIO_NUM,  .pin_reset    = RESET_GPIO_NUM,
        .pin_xclk     = XCLK_GPIO_NUM,
        .pin_sccb_sda = SIOD_GPIO_NUM,  .pin_sccb_scl = SIOC_GPIO_NUM,
        .pin_d7 = Y9_GPIO_NUM,  .pin_d6 = Y8_GPIO_NUM,
        .pin_d5 = Y7_GPIO_NUM,  .pin_d4 = Y6_GPIO_NUM,
        .pin_d3 = Y5_GPIO_NUM,  .pin_d2 = Y4_GPIO_NUM,
        .pin_d1 = Y3_GPIO_NUM,  .pin_d0 = Y2_GPIO_NUM,
        .pin_vsync = VSYNC_GPIO_NUM, .pin_href = HREF_GPIO_NUM,
        .pin_pclk  = PCLK_GPIO_NUM,
        .xclk_freq_hz = 20000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size   = FRAMESIZE_240X240,
        .jpeg_quality = 12,
        .fb_count     = 1,
        .fb_location  = CAMERA_FB_IN_PSRAM,
        .grab_mode    = CAMERA_GRAB_LATEST,
    };
    if (esp_camera_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed");
        return false;
    }
    sensor_t *s = esp_camera_sensor_get();
    s->set_brightness(s, 2);
    s->set_contrast(s, 1);
    s->set_saturation(s, -1);
    s->set_gainceiling(s, GAINCEILING_128X);
    s->set_whitebal(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_ae_level(s, 2);
    s->set_hmirror(s, 1);
    s->set_vflip(s, 1);
    ESP_LOGI(TAG, "Camera OK");
    return true;
}

IRAM_ATTR void rgb565_to_rgb888_gray(
    const uint8_t *src, int src_w, int src_h,
    uint8_t *dst_rgb, uint8_t *dst_gray, int dst_w, int dst_h)
{
    const int FP = 16;
    int sx = (src_w << FP) / dst_w, sy = (src_h << FP) / dst_h;
    int sc = (sx < sy) ? sx : sy;
    int cw = (dst_w * sc) >> FP, ch = (dst_h * sc) >> FP;
    int ox = (src_w - cw) / 2, oy = (src_h - ch) / 2;

    for (int y = 0; y < dst_h; y++) {
        int row = oy + ((y * sc) >> FP);
        if (row >= src_h) row = src_h - 1;
        const uint8_t *srow  = src      + row * src_w * 2;
        uint8_t       *drgb  = dst_rgb  + y * dst_w * 3;
        uint8_t       *dgray = dst_gray + y * dst_w;
        for (int x = 0; x < dst_w; x++) {
            int col = ox + ((x * sc) >> FP);
            if (col >= src_w) col = src_w - 1;
            const uint8_t *p = srow + col * 2;
            uint16_t px = (uint16_t)(p[0] | (p[1] << 8));
            uint8_t r = (uint8_t)((px >> 8) & 0xF8u);
            uint8_t g = (uint8_t)((px >> 3) & 0xFCu);
            uint8_t b = (uint8_t)((px << 3) & 0xF8u);
            drgb[x * 3 + 0] = r;
            drgb[x * 3 + 1] = g;
            drgb[x * 3 + 2] = b;
            dgray[x] = (uint8_t)((77u * r + 150u * g + 29u * b) >> 8);
        }
    }
}
