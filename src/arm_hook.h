#ifndef NEST_MQTT_ARM_HOOK_H
#define NEST_MQTT_ARM_HOOK_H

#include <stddef.h>

bool arm_install_hook(void *target, void *replacement, void **trampoline,
                      char *error, size_t error_size);

#endif
