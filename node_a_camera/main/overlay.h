#pragma once
#include <stdint.h>
#include "tracker.h"

void overlay_update_boxes(const tracked_object_t *tracks, int count);
void draw_overlay(uint8_t *buf, int fw, int fh);
