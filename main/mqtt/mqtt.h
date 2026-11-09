#ifndef MQTT_H
#define MQTT_H

#include <stdbool.h>
#include <stdint.h>

void mqtt_init(void);

bool mqtt_is_connected(void);

int64_t mqtt_get_last_command_time(void);

#endif