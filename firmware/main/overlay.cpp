#include "overlay.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define MAX_OVL_BOXES 16

static struct { int x1, y1, x2, y2; int cls; } g_ovl_boxes[MAX_OVL_BOXES];
static uint32_t     g_ovl_box_count = 0;
static portMUX_TYPE g_ovl_mux = portMUX_INITIALIZER_UNLOCKED;

void overlay_update_boxes(const tracked_object_t *tracks, int count)
{
    portENTER_CRITICAL(&g_ovl_mux);
    g_ovl_box_count = 0;
    for (int t = 0; t < count && (int)g_ovl_box_count < MAX_OVL_BOXES; t++) {
        // Display-level ghost filter: suppress a track that has been seen
        // many times but barely moved — it is a static background region.
        // This kicks in sooner than the tracker expiry to clean up the overlay
        // quickly (within ~1 s of the background detection appearing).
        if (tracks[t].detection_count >= GHOST_DISP_FRAMES &&
            tracks[t].travel < GHOST_TRAVEL_THRESHOLD)
            continue;
        g_ovl_boxes[g_ovl_box_count++] = {
            tracks[t].box[0], tracks[t].box[1],
            tracks[t].box[2], tracks[t].box[3],
            tracks[t].class_id
        };
    }
    portEXIT_CRITICAL(&g_ovl_mux);
}

static inline void set_pixel(uint8_t *buf, int w, int h, int x, int y, uint16_t c)
{
    if ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) {
        buf[(y * w + x) * 2 + 0] = (uint8_t)(c & 0xFF);
        buf[(y * w + x) * 2 + 1] = (uint8_t)(c >> 8);
    }
}

static void draw_rect(uint8_t *buf, int fw, int fh,
                      int x1, int y1, int x2, int y2, uint16_t col)
{
    for (int t = 0; t < 3; t++) {
        for (int x = x1; x <= x2; x++) {
            set_pixel(buf, fw, fh, x, y1 + t, col);
            set_pixel(buf, fw, fh, x, y2 - t, col);
        }
        for (int y = y1 + t; y <= y2 - t; y++) {
            set_pixel(buf, fw, fh, x1 + t, y, col);
            set_pixel(buf, fw, fh, x2 - t, y, col);
        }
    }
    for (int dy = 0; dy < 5; dy++)
        for (int dx = 0; dx < 5; dx++) {
            set_pixel(buf, fw, fh, x1 + dx, y1 + dy, col);
            set_pixel(buf, fw, fh, x2 - dx, y1 + dy, col);
            set_pixel(buf, fw, fh, x1 + dx, y2 - dy, col);
            set_pixel(buf, fw, fh, x2 - dx, y2 - dy, col);
        }
}

void draw_overlay(uint8_t *buf, int fw, int fh)
{
    const int FP = 16;
    int sx = (fw << FP) / MODEL_W, sy = (fh << FP) / MODEL_H;
    int sc = (sx < sy) ? sx : sy;
    int ox = (fw - ((MODEL_W * sc) >> FP)) / 2;
    int oy = (fh - ((MODEL_H * sc) >> FP)) / 2;
    const uint16_t cls_col[2] = {0x001Fu, 0xF800u}; // car=blue, moto=red

    struct { int x1, y1, x2, y2; uint16_t col; } rects[MAX_OVL_BOXES];
    uint32_t cnt;
    portENTER_CRITICAL(&g_ovl_mux);
    cnt = g_ovl_box_count;
    for (uint32_t b = 0; b < cnt; b++) {
        rects[b].x1  = ox + ((g_ovl_boxes[b].x1 * sc) >> FP);
        rects[b].y1  = oy + ((g_ovl_boxes[b].y1 * sc) >> FP);
        rects[b].x2  = ox + ((g_ovl_boxes[b].x2 * sc) >> FP);
        rects[b].y2  = oy + ((g_ovl_boxes[b].y2 * sc) >> FP);
        rects[b].col = cls_col[g_ovl_boxes[b].cls < NUM_CLASSES ? g_ovl_boxes[b].cls : 0];
    }
    portEXIT_CRITICAL(&g_ovl_mux);

    for (uint32_t b = 0; b < cnt; b++)
        draw_rect(buf, fw, fh, rects[b].x1, rects[b].y1, rects[b].x2, rects[b].y2, rects[b].col);

    // Draw the counting zone as two dashed yellow lines (top + bottom of band)
    int zone_top_y    = oy + ((COUNT_ZONE_TOP    * sc) >> FP);
    int zone_bottom_y = oy + ((COUNT_ZONE_BOTTOM * sc) >> FP);
    for (int x = 0; x < fw; x++) {
        // dash pattern: 8 px on, 4 px off
        uint16_t col = ((x / 8) % 2 == 0) ? 0xFFE0u : 0x0000u;
        set_pixel(buf, fw, fh, x, zone_top_y,        col);
        set_pixel(buf, fw, fh, x, zone_top_y    + 1, col);
        set_pixel(buf, fw, fh, x, zone_bottom_y,     col);
        set_pixel(buf, fw, fh, x, zone_bottom_y + 1, col);
    }
}
