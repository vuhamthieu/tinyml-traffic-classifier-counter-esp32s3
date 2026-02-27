#pragma once
#include "secrets.h"

#define MQTT_TOPIC_COUNTS "traffic/counts"
#define MQTT_TOPIC_STATUS "traffic/status"

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

#define CLASS_BIG_VEHICLE   0
#define CLASS_CAR           1
#define CLASS_MOTORCYCLE    2
#define NUM_CLASSES         3

#define SCORE_THR_BIG    0.75f
#define SCORE_THR_CAR    0.55f
#define SCORE_THR_MOTO   0.60f
#define NMS_THR          0.45f

#define TRACK_TIMEOUT_FRAMES    8
#define TRACK_MATCH_DISTANCE    100
#define TRACK_EXCLUSION_RADIUS  28
#define MAX_TRACKED_OBJECTS     24

#define COUNT_LINE_Y        (MODEL_H / 2)
#define COUNT_LINE_DEADZONE 12

#define MOTION_CELL_SIZE   8
#define MOTION_GRID_W      (MODEL_W / MOTION_CELL_SIZE)
#define MOTION_GRID_H      (MODEL_H / MOTION_CELL_SIZE)
#define MOTION_DIFF_THRESH 15
