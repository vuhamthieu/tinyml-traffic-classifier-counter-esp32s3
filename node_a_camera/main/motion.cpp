#include "motion.h"
#include "esp_heap_caps.h"
#include <string.h>

static uint8_t *g_prev_gray = nullptr;

void compute_motion_mask(const uint8_t *curr, uint8_t mask[MOTION_GRID_H][MOTION_GRID_W])
{
    if (!g_prev_gray) {
        g_prev_gray = (uint8_t *)heap_caps_malloc(
            MODEL_W * MODEL_H, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!g_prev_gray)
            g_prev_gray = (uint8_t *)heap_caps_malloc(
                MODEL_W * MODEL_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        memset(mask, 1, MOTION_GRID_H * MOTION_GRID_W);
        return;
    }
    for (int gy = 0; gy < MOTION_GRID_H; ++gy) {
        for (int gx = 0; gx < MOTION_GRID_W; ++gx) {
            uint16_t sum = 0;
            const int base = (gy * MOTION_CELL_SIZE) * MODEL_W + gx * MOTION_CELL_SIZE;
            for (int ky = 0; ky < MOTION_CELL_SIZE; ++ky) {
                const int row = base + ky * MODEL_W;
                for (int kx = 0; kx < MOTION_CELL_SIZE; ++kx) {
                    uint8_t a = curr[row + kx], b = g_prev_gray[row + kx];
                    sum += (uint16_t)((a > b) ? (a - b) : (b - a));
                }
            }
            mask[gy][gx] = ((sum >> 6) >= MOTION_DIFF_THRESH) ? 1u : 0u;
        }
    }
}

void motion_update_prev(const uint8_t *gray)
{
    if (g_prev_gray && gray)
        memcpy(g_prev_gray, gray, MODEL_W * MODEL_H);
}

bool detection_has_motion(int cx, int cy, uint8_t mask[MOTION_GRID_H][MOTION_GRID_W])
{
    int cell_x = cx / MOTION_CELL_SIZE, cell_y = cy / MOTION_CELL_SIZE;
    int active = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        int ny = cell_y + dy;
        if (ny < 0 || ny >= MOTION_GRID_H) continue;
        for (int dx = -1; dx <= 1; ++dx) {
            int nx = cell_x + dx;
            if (nx < 0 || nx >= MOTION_GRID_W) continue;
            active += mask[ny][nx];
        }
    }
    return active >= 2;
}
