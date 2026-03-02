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
    if (c == CLASS_CAR) return "car";
    if (c == CLASS_MOTORCYCLE) return "motorcycle";
    return "unknown";
}

float class_score_thr(int c)
{
    if (c == CLASS_CAR) return SCORE_THR_CAR;
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
    (void)motion_mask;  // shake guard is in main.cpp; per-detection gate removed (see analysis)
    for (int i = 0; i < num_tracked_objects; i++)
        tracked_objects[i].frames_since_seen++;

    // One-to-one assignment guard per frame:
    // each existing track can be matched by at most one detection in this frame.
    bool matched_in_frame[MAX_TRACKED_OBJECTS] = {false};

    for (const auto &det : results) {
        int class_id = det.category;
        if (class_id < 0 || class_id >= NUM_CLASSES) continue;
        if ((int)det.box.size() < 4)                 continue;

        int bw   = det.box[2] - det.box[0];
        int bh   = det.box[3] - det.box[1];
        int area = bw * bh;

        // Size-based symmetric reclassification:
        // - very small "car" boxes are likely distant motorcycles
        // - large AND wide enough "motorcycle" boxes are cars mislabeled by the model
        if (class_id == CLASS_CAR && area < MOTO_RECLASSIFY_AREA) {
            class_id = CLASS_MOTORCYCLE;
        } else if (class_id == CLASS_MOTORCYCLE
                   && area >= MOTO_TO_CAR_RECLASSIFY_AREA
                   && bw >= MOTO_TO_CAR_RECLASSIFY_MIN_W) {
            class_id = CLASS_CAR;
        }

        // Raw model output log — shows post-reclassification label.
        ESP_LOGW(TAG, "RAW %s(%.2f) box=[%d,%d,%d,%d] %dx%d area=%d",
                 class_name(class_id), det.score,
                 det.box[0], det.box[1], det.box[2], det.box[3],
                 bw, bh, area);

        if (det.score < class_score_thr(class_id))   continue;
        if (bw < 1 || bh < 1 || area < DETECT_MIN_BOX_AREA) continue;

        // Corner-artifact filter: a box pinned to 2+ perpendicular image edges
        // is road-marking / border noise, not a real vehicle.
        {
            const int x1 = det.box[0], y1 = det.box[1],
                      x2 = det.box[2], y2 = det.box[3];
            int clips = (x1 <= 2 ? 1 : 0) + (y1 <= 2 ? 1 : 0) +
                        (x2 >= MODEL_W - 3 ? 1 : 0) + (y2 >= MODEL_H - 3 ? 1 : 0);
            if (clips >= 2) continue;
        }

        // Thin-box filter — min-dim lowered 14 → 7 to preserve narrow motorcycle
        // bounding boxes (8-13 px wide); the area gate above already rejects
        // truly tiny noise.
        if (bw < 7 || bh < 7 || bw > 5 * bh || bh > 5 * bw) continue;

        int cx = (det.box[0] + det.box[2]) / 2;
        int cy = (det.box[1] + det.box[3]) / 2;

        int   best   = -1;
        float best_d = (float)TRACK_MATCH_DISTANCE;

        for (int t = 0; t < num_tracked_objects; t++) {
            if (matched_in_frame[t]) continue;
            // Match purely by proximity — ignore class label.
            // The model flips car↔motorcycle↔big_vehicle across frames for the
            // same physical object.  Enforcing class equality causes a real car
            // to spawn a new 1-detection track every time its label flips,
            // so the track never accumulates enough detections to count.
            int px = tracked_objects[t].x, py = tracked_objects[t].y;
            if (tracked_objects[t].detection_count > 1) {
                px += tracked_objects[t].vx * tracked_objects[t].frames_since_seen;
                py += tracked_objects[t].vy * tracked_objects[t].frames_since_seen;
            }
            float d = calc_dist(cx, cy, px, py);
            // Tighten match radius for already-counted tracks:
            // a following vehicle in the same lane must not be absorbed
            // into the exiting counted track — force it to spawn fresh.
            float max_d = tracked_objects[t].counted
                          ? (float)COUNTED_TRACK_MATCH_DISTANCE
                          : (float)TRACK_MATCH_DISTANCE;
            if (d < max_d && d < best_d) { best_d = d; best = t; }
        }

        if (best >= 0) {
            auto &tk = tracked_objects[best];
            tk.prev_x = tk.x;  tk.prev_y = tk.y;
            tk.x = cx;         tk.y = cy;
            tk.frames_since_seen = 0;
            tk.detection_count++;
            matched_in_frame[best] = true;
            if (det.score > tk.best_conf) {
                tk.best_conf = det.score;
                tk.class_id  = class_id;  // settle on whichever label scores highest
            }

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

            // EMA-smooth the bounding box: 55 % new detection + 45 % history.
            // Eliminates the per-frame box jitter caused by model output variation
            // while still tracking the vehicle quickly.
            const float BA = 0.55f;
            tk.box[0] = (int)(BA * det.box[0] + (1.f - BA) * tk.box[0]);
            tk.box[1] = (int)(BA * det.box[1] + (1.f - BA) * tk.box[1]);
            tk.box[2] = (int)(BA * det.box[2] + (1.f - BA) * tk.box[2]);
            tk.box[3] = (int)(BA * det.box[3] + (1.f - BA) * tk.box[3]);

            // Presence-based counting: fire once the vehicle is confirmed inside
            // the zone.
            //   confirmed — dc >= COUNT_MIN_DETECTIONS rejects single-frame flickers.
            //   moved     — travel >= COUNT_MIN_TRAVEL (12 px) distinguishes real
            //               motion from static background FPs (jitter ≤5 px).
            // Shake is already gated in main.cpp before calling this fn.
            bool in_zone  = (tk.y >= COUNT_ZONE_TOP && tk.y <= COUNT_ZONE_BOTTOM);
            bool confirmed = (tk.detection_count >= COUNT_MIN_DETECTIONS);
            bool moved     = (tk.travel >= COUNT_MIN_TRAVEL);
            if (!tk.counted && in_zone && confirmed && moved) {
                tk.counted = true;
                int count_class = tk.class_id;
                int cbw = tk.box[2] - tk.box[0];
                int cbh = tk.box[3] - tk.box[1];
                int carea = cbw * cbh;
                if (count_class == CLASS_MOTORCYCLE && carea >= MOTO_TO_CAR_RECLASSIFY_AREA)
                    count_class = CLASS_CAR;
                xSemaphoreTake(counter_mutex, portMAX_DELAY);
                vehicle_counts[count_class]++;
                uint32_t new_count = vehicle_counts[count_class];
                xSemaphoreGive(counter_mutex);
                if (s_count_cb) s_count_cb(count_class, new_count);
                ESP_LOGW(TAG, "COUNTED %s #%lu (%.2f) dc=%d travel=%.0f area=%d",
                         class_name(count_class), (unsigned long)new_count,
                         tk.best_conf, tk.detection_count, tk.travel);
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

            tracked_objects[num_tracked_objects] = {
                cx, cy, cx, cy, cx, cy,
                class_id, 0, 1, 0.0f, 0, 0, 0,
                (cy / MOTION_CELL_SIZE) * MOTION_GRID_W + (cx / MOTION_CELL_SIZE),
                0, false, det.score,
                {det.box[0], det.box[1], det.box[2], det.box[3]}
            };
            matched_in_frame[num_tracked_objects] = true;
            num_tracked_objects++;
            ESP_LOGD(TAG, "SPAWN %s(%.2f) @(%d,%d)", class_name(class_id), det.score, cx, cy);
        }
    }

    int w = 0;
    for (int i = 0; i < num_tracked_objects; i++) {
        // Ghost suppressor: static background false positives are matched many
        // times (high detection_count) but never move far from where they
        // spawned (low travel).  Discard them silently — no fast-count either.
        if (tracked_objects[i].detection_count >= GHOST_FRAMES_THRESHOLD &&
            tracked_objects[i].travel < GHOST_TRAVEL_THRESHOLD) {
            ESP_LOGD(TAG, "GHOST expired %s dc=%d travel=%.0f",
                     class_name(tracked_objects[i].class_id),
                     tracked_objects[i].detection_count,
                     tracked_objects[i].travel);
            continue;
        }
        if (tracked_objects[i].frames_since_seen < TRACK_TIMEOUT_FRAMES) {
            // Predict box position using velocity for up to 3 missed frames.
            // Keeps the overlay box following the vehicle through brief detection
            // gaps (truncated frame, partial occlusion, model skip).
            int fseen = tracked_objects[i].frames_since_seen;
            if (fseen > 0 && fseen <= 3 && tracked_objects[i].detection_count > 2) {
                int pvx = tracked_objects[i].vx;
                int pvy = tracked_objects[i].vy;
                for (int b = 0; b < 4; b += 2) {
                    tracked_objects[i].box[b]   = std::max(0, std::min(MODEL_W - 1,
                                                    tracked_objects[i].box[b]   + pvx));
                    tracked_objects[i].box[b+1] = std::max(0, std::min(MODEL_H - 1,
                                                    tracked_objects[i].box[b+1] + pvy));
                }
            }
            tracked_objects[w++] = tracked_objects[i];
        } else {
            const int EZ = MODEL_W / 5;
            bool near_edge = (tracked_objects[i].spawn_x < EZ ||
                              tracked_objects[i].spawn_x > MODEL_W - EZ ||
                              tracked_objects[i].spawn_y < EZ ||
                              tracked_objects[i].spawn_y > MODEL_H - EZ);
            if (!tracked_objects[i].counted &&
                tracked_objects[i].best_conf >= 0.65f && near_edge &&
                tracked_objects[i].travel >= FAST_COUNT_MIN_TRAVEL &&
                tracked_objects[i].cell_changes >= 2) {
                int cid = tracked_objects[i].class_id;
                int bw = tracked_objects[i].box[2] - tracked_objects[i].box[0];
                int bh = tracked_objects[i].box[3] - tracked_objects[i].box[1];
                int area = bw * bh;
                if (cid == CLASS_MOTORCYCLE && area >= MOTO_TO_CAR_RECLASSIFY_AREA)
                    cid = CLASS_CAR;
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
