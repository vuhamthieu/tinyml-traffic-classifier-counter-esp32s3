#include "telemetry.h"
#include "config.h"
#include "mqtt.h"
#include "lora.h"
#include "tracker.h"

void telemetry_init(void)
{
#if TELEMETRY_USE_MQTT
    mqtt_init();
#endif
#if TELEMETRY_USE_LORA
    lora_init();
#endif
}

void telemetry_publish_all(void)
{
    static uint32_t last_sent_car = 0;
    static uint32_t last_sent_moto = 0;

    xSemaphoreTake(counter_mutex, portMAX_DELAY);
    uint32_t current_car = vehicle_counts[CLASS_CAR];
    uint32_t current_moto = vehicle_counts[CLASS_MOTORCYCLE];
    xSemaphoreGive(counter_mutex);

    uint32_t delta_car = current_car - last_sent_car;
    uint32_t delta_moto = current_moto - last_sent_moto;

    last_sent_car = current_car;
    last_sent_moto = current_moto;

#if TELEMETRY_USE_MQTT
    mqtt_publish_all(delta_car, delta_moto);
#endif
#if TELEMETRY_USE_LORA
    lora_publish_counts(delta_car, delta_moto);
#endif
}
