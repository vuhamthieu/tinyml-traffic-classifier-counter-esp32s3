#pragma once
#include <stdint.h>
#include <stdbool.h>

bool       camera_init(void);
void       rgb565_to_rgb888_gray(const uint8_t *src, int src_w, int src_h,
                                 uint8_t *dst_rgb, uint8_t *dst_gray,
                                 int dst_w, int dst_h);
