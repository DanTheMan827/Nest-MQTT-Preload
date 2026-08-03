#ifndef NEST_MQTT_BRIDGE_H
#define NEST_MQTT_BRIDGE_H

#include <stdarg.h>
#include <string>

void bridge_bootstrap();
void bridge_shutdown();
void bridge_ingest_log(const char *line);
bool bridge_should_block_sleep();
void bridge_sleep_blocked();

#endif
