#include "jpeg_stream.h"
#include "esp_camera.h"
#include "img_converters.h"
#include <stdlib.h>

uint8_t          *g_jpg_buf       = nullptr;
size_t            g_jpg_len       = 0;
SemaphoreHandle_t g_jpg_mutex     = nullptr;
volatile bool     g_stream_active = false;

void jpeg_stream_init(void)
{
    g_jpg_mutex = xSemaphoreCreateMutex();
}

void jpeg_stream_encode(const uint8_t *raw_rgb565, size_t raw_len, int w, int h)
{
    if (!g_jpg_mutex) return;
    if (xSemaphoreTake(g_jpg_mutex, pdMS_TO_TICKS(300)) != pdTRUE) return;
    if (g_jpg_buf) { free(g_jpg_buf); g_jpg_buf = nullptr; g_jpg_len = 0; }
    fmt2jpg((uint8_t *)raw_rgb565, raw_len, (uint16_t)w, (uint16_t)h,
            PIXFORMAT_RGB565, 70, &g_jpg_buf, &g_jpg_len);
    xSemaphoreGive(g_jpg_mutex);
}
