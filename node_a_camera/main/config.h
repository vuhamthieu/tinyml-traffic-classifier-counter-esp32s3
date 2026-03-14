#pragma once
#include "secrets.h"

#define MQTT_TOPIC_COUNTS "traffic/counts"
#define MQTT_TOPIC_STATUS "traffic/status"

// Telemetry transport selection.
#define TELEMETRY_USE_MQTT 0
#define TELEMETRY_USE_LORA 1

// LoRa SX1278 wiring.
#define LORA_PIN_SCK    40
#define LORA_PIN_MISO   39
#define LORA_PIN_MOSI   38
#define LORA_PIN_NSS    42
#define LORA_PIN_RST    21
#define LORA_PIN_DIO0   14

// Set to 1 for the standalone LoRa ping test.
#define LORA_TEST_MODE  0

#define LORA_FREQ_HZ        433000000UL
#define LORA_BW_INDEX       7
#define LORA_SF             7
#define LORA_CR_DENOM       5
#define LORA_SYNC_WORD      0x12

#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   15
#define SIOD_GPIO_NUM    4
#define SIOC_GPIO_NUM    5
#define Y9_GPIO_NUM     16
#define Y8_GPIO_NUM     17
#define Y7_GPIO_NUM     18
#define Y6_GPIO_NUM     12
#define Y5_GPIO_NUM     10
#define Y4_GPIO_NUM      8
#define Y3_GPIO_NUM      9
#define Y2_GPIO_NUM     11
#define VSYNC_GPIO_NUM   6
#define HREF_GPIO_NUM    7
#define PCLK_GPIO_NUM   13

#define CAMERA_W    240
#define CAMERA_H    240
#define MODEL_W     224
#define MODEL_H     224

// OV3660 RGB565 byte order coming from esp_camera frame buffer.
// 1 = big-endian (MSB first), 0 = little-endian (LSB first)
#define CAMERA_RGB565_BIG_ENDIAN 1

// 2-class model: car=0, motorcycle=1
#define CLASS_CAR           0
#define CLASS_MOTORCYCLE    1
#define NUM_CLASSES         2

#define SCORE_THR_CAR    0.20f 
#define SCORE_THR_MOTO   0.25f
#define NMS_THR          0.40f

#define TRACK_TIMEOUT_FRAMES    18   
#define TRACK_MATCH_DISTANCE    96   
#define TRACK_EXCLUSION_RADIUS  18
#define MAX_TRACKED_OBJECTS     24
// Prevent an already-counted track from absorbing a following vehicle.
// If a counted track is farther than this from a new detection, force spawn.
#define COUNTED_TRACK_MATCH_DISTANCE 42
#define COUNT_LINE_Y      (MODEL_H * 3 / 8)   
// Legacy zone boundaries kept for fast-count edge heuristic reference.
#define COUNT_ZONE_TOP    (MODEL_H / 4)      
#define COUNT_ZONE_BOTTOM (MODEL_H * 3 / 4)  
#define COUNT_MIN_TRAVEL      8.0f   // lower for far/small vehicles so they can be counted earlier
#define COUNT_MIN_DETECTIONS  2      // faster confirmation to improve recall

#define MOTION_CELL_SIZE   8
#define MOTION_GRID_W      (MODEL_W / MOTION_CELL_SIZE)
#define MOTION_GRID_H      (MODEL_H / MOTION_CELL_SIZE)
#define MOTION_DIFF_THRESH 15

// Detection quality filters
#define DETECT_MIN_BOX_AREA   110    
#define MOTO_RECLASSIFY_AREA  260   
#define MOTO_TO_CAR_RECLASSIFY_AREA 1600 
#define MOTO_TO_CAR_RECLASSIFY_MIN_W 24 
#define FAST_COUNT_MIN_TRAVEL 18.0f  

// Ghost-track suppressor: a track matched ≥ GHOST_FRAMES times but moved
// less than GHOST_TRAVEL pixels from its spawn is a static background
// false positive and is silently discarded.
#define GHOST_FRAMES_THRESHOLD  10     
#define GHOST_TRAVEL_THRESHOLD  25.0f  
// Display suppression kicks in sooner: hide a track after GHOST_DISP_FRAMES
// consecutive detections with travel < GHOST_TRAVEL_THRESHOLD so the box
// disappears from the overlay quickly without waiting for full expiry.
#define GHOST_DISP_FRAMES       8      // ~1.3 s at 6 FPS
