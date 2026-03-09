#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

void compute_motion_mask(const uint8_t *curr, uint8_t mask[MOTION_GRID_H][MOTION_GRID_W]);
void motion_update_prev(const uint8_t *gray);
bool detection_has_motion(int cx, int cy, uint8_t mask[MOTION_GRID_H][MOTION_GRID_W]);
