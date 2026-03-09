#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern uint8_t          *g_jpg_buf;
extern size_t            g_jpg_len;
extern SemaphoreHandle_t g_jpg_mutex;
extern volatile bool     g_stream_active;

void jpeg_stream_init(void);
void jpeg_stream_encode(const uint8_t *raw_rgb565, size_t raw_len, int w, int h);

extern uint8_t          *g_debug_jpg_buf;
extern size_t            g_debug_jpg_len;
extern SemaphoreHandle_t g_debug_mutex;
void jpeg_encode_debug_frame(const uint8_t *rgb888, int w, int h);
