#pragma once
#include <stdint.h>

void mqtt_init(void);
void mqtt_publish_all(uint32_t delta_car, uint32_t delta_moto);
