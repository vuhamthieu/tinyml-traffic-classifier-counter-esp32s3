#pragma once
#include <stdint.h>

void lora_init(void);
void lora_publish_counts(uint32_t delta_car, uint32_t delta_moto);
void lora_test_ping_loop(void);
