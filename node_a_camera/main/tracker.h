#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <list>
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "dl_detect_base.hpp"

typedef struct {
    int   x, y, prev_x, prev_y, spawn_x, spawn_y;
    int   class_id, frames_since_seen, detection_count;
    float travel;
    int   vx, vy, dir_ok, last_cell, cell_changes;
    bool  counted;
    float best_conf;
    int   box[4];
} tracked_object_t;

extern tracked_object_t  tracked_objects[MAX_TRACKED_OBJECTS];
extern int               num_tracked_objects;
extern uint32_t          vehicle_counts[NUM_CLASSES];
extern SemaphoreHandle_t counter_mutex;
extern float             current_fps;
extern int               current_tracking;

typedef void (*vehicle_counted_cb_t)(int class_id, uint32_t new_count);
void tracker_set_count_callback(vehicle_counted_cb_t cb);

void        update_tracking(const std::list<dl::detect::result_t> &results,
                             uint8_t motion_mask[MOTION_GRID_H][MOTION_GRID_W]);
const char *class_name(int c);
float       class_score_thr(int c);
