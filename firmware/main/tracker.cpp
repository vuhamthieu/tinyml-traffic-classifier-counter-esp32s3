#include "tracker.h"
#include "motion.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "TRACKER";

tracked_object_t  tracked_objects[MAX_TRACKED_OBJECTS];
int               num_tracked_objects = 0;
uint32_t          vehicle_counts[NUM_CLASSES] = {0};
SemaphoreHandle_t counter_mutex    = nullptr;
float             current_fps      = 0.0f;
int               current_tracking = 0;

static vehicle_counted_cb_t s_count_cb = nullptr;

void tracker_set_count_callback(vehicle_counted_cb_t cb) { s_count_cb = cb; }

const char *class_name(int c)
{
    if (c == CLASS_BIG_VEHICLE) return "big_vehicle";
    if (c == CLASS_CAR)         return "car";
    return "motorcycle";
}

float class_score_thr(int c)
{
    if (c == CLASS_BIG_VEHICLE) return SCORE_THR_BIG;
    if (c == CLASS_CAR)         return SCORE_THR_CAR;
    return SCORE_THR_MOTO;
}

static inline float calc_dist(int x1, int y1, int x2, int y2)
{
    int dx = x2 - x1, dy = y2 - y1;
    return sqrtf((float)(dx * dx + dy * dy));
}

void update_tracking(const std::list<dl::detect::result_t> &results,
                     uint8_t motion_mask[MOTION_GRID_H][MOTION_GRID_W])
{
    for (int i = 0; i < num_tracked_objects; i++)
        tracked_objects[i].frames_since_seen++;

    for (const auto &det : results) {
        int class_id = det.category;
        if (class_id < 0 || class_id >= NUM_CLASSES) continue;
        if (det.score < class_score_thr(class_id))   continue;
        if ((int)det.box.size() < 4)                 continue;

        int cx = (det.box[0] + det.box[2]) / 2;
        int cy = (det.box[1] + det.box[3]) / 2;

        int   best   = -1;
        float best_d = (float)TRACK_MATCH_DISTANCE;

        for (int t = 0; t < num_tracked_objects; t++) {
            if (tracked_objects[t].class_id != class_id) continue;
            int px = tracked_objects[t].x, py = tracked_objects[t].y;
            if (tracked_objects[t].detection_count > 1) {
                px += tracked_objects[t].vx * tracked_objects[t].frames_since_seen;
                py += tracked_objects[t].vy * tracked_objects[t].frames_since_seen;
            }
            float d = calc_dist(cx, cy, px, py);
            if (d < best_d) { best_d = d; best = t; }
        }

        if (best >= 0) {
            auto &tk = tracked_objects[best];
            tk.prev_x = tk.x;  tk.prev_y = tk.y;
            tk.x = cx;         tk.y = cy;
            tk.frames_since_seen = 0;
            tk.detection_count++;
            if (det.score > tk.best_conf) tk.best_conf = det.score;

            int cur_cell = (cy / MOTION_CELL_SIZE) * MOTION_GRID_W + (cx / MOTION_CELL_SIZE);
            if (cur_cell != tk.last_cell) { tk.cell_changes++; tk.last_cell = cur_cell; }

            int tdx = cx - tk.spawn_x, tdy = cy - tk.spawn_y;
            tk.travel = sqrtf((float)(tdx * tdx + tdy * tdy));

            int nvx = cx - tk.prev_x, nvy = cy - tk.prev_y;
            if (tk.detection_count > 2) {
                int dot = nvx * tk.vx + nvy * tk.vy;
                tk.dir_ok = (dot > 0) ? tk.dir_ok + 1 : 0;
            }
            tk.vx = nvx; tk.vy = nvy;
            tk.box[0] = det.box[0]; tk.box[1] = det.box[1];
            tk.box[2] = det.box[2]; tk.box[3] = det.box[3];

            int prev_y_dist = abs(tk.prev_y - COUNT_LINE_Y);
            bool crossed =
                tk.detection_count >= 2 &&
                prev_y_dist >= COUNT_LINE_DEADZONE &&
                ((tk.prev_y < COUNT_LINE_Y && tk.y >= COUNT_LINE_Y) ||
                 (tk.prev_y > COUNT_LINE_Y && tk.y <= COUNT_LINE_Y));
            bool has_motion = detection_has_motion(cx, cy, motion_mask);
            if (!tk.counted && crossed && has_motion) {
                tk.counted = true;
                xSemaphoreTake(counter_mutex, portMAX_DELAY);
                vehicle_counts[class_id]++;
                uint32_t new_count = vehicle_counts[class_id];
                xSemaphoreGive(counter_mutex);
                if (s_count_cb) s_count_cb(class_id, new_count);
                ESP_LOGW(TAG, "COUNTED %s #%lu (%.2f) y:%d->%d",
                         class_name(class_id), (unsigned long)new_count,
                         tk.best_conf, tk.prev_y, tk.y);
            }

        } else if (num_tracked_objects < MAX_TRACKED_OBJECTS) {
            bool too_close = false;
            for (int t = 0; t < num_tracked_objects; t++) {
                if (tracked_objects[t].class_id == class_id &&
                    calc_dist(cx, cy, tracked_objects[t].x, tracked_objects[t].y)
                        < (float)TRACK_EXCLUSION_RADIUS) {
                    too_close = true; break;
                }
            }
            if (too_close) continue;

            tracked_objects[num_tracked_objects++] = {
                cx, cy, cx, cy, cx, cy,
                class_id, 0, 1, 0.0f, 0, 0, 0,
                (cy / MOTION_CELL_SIZE) * MOTION_GRID_W + (cx / MOTION_CELL_SIZE),
                0, false, det.score,
                {det.box[0], det.box[1], det.box[2], det.box[3]}
            };
            ESP_LOGD(TAG, "SPAWN %s(%.2f) @(%d,%d)", class_name(class_id), det.score, cx, cy);
        }
    }

    int w = 0;
    for (int i = 0; i < num_tracked_objects; i++) {
        if (tracked_objects[i].frames_since_seen < TRACK_TIMEOUT_FRAMES) {
            tracked_objects[w++] = tracked_objects[i];
        } else {
            const int EZ = MODEL_W / 5;
            bool near_edge = (tracked_objects[i].spawn_x < EZ ||
                              tracked_objects[i].spawn_x > MODEL_W - EZ ||
                              tracked_objects[i].spawn_y < EZ ||
                              tracked_objects[i].spawn_y > MODEL_H - EZ);
            if (!tracked_objects[i].counted &&
                tracked_objects[i].best_conf >= 0.82f && near_edge) {
                int cid = tracked_objects[i].class_id;
                xSemaphoreTake(counter_mutex, portMAX_DELAY);
                vehicle_counts[cid]++;
                uint32_t new_count = vehicle_counts[cid];
                xSemaphoreGive(counter_mutex);
                if (s_count_cb) s_count_cb(cid, new_count);
                ESP_LOGW(TAG, "COUNTED(fast) %s #%lu (%.2f)",
                         class_name(cid), (unsigned long)new_count,
                         tracked_objects[i].best_conf);
            }
        }
    }
    num_tracked_objects = w;
    current_tracking    = w;
}
