#ifndef NEST_MQTT_NATIVE_NLCLIENT_H
#define NEST_MQTT_NATIVE_NLCLIENT_H

#include <string>

struct NativeSnapshot {
    bool valid;
    bool current_temperature_valid;
    double current_temperature_c;
    bool target_temperature_valid;
    double target_temperature_c;
    bool target_low_valid;
    double target_low_c;
    bool target_high_valid;
    double target_high_c;
    bool battery_valid;
    double battery_level;
    bool mode_valid;
    int native_mode;
    bool emergency_heat_valid;
    bool emergency_heat;
    bool eco_valid;
    int eco_mode;

    NativeSnapshot();
};

enum NativeCommandKind {
    NATIVE_SET_TARGET,
    NATIVE_SET_TARGET_LOW,
    NATIVE_SET_TARGET_HIGH,
    NATIVE_SET_MODE,
    NATIVE_SET_EMERGENCY_HEAT,
    NATIVE_SET_ECO
};

bool native_initialize(std::string *status);
bool native_hooks_active();
bool native_poll(NativeSnapshot *snapshot);
bool native_command(NativeCommandKind kind, double value, std::string *error);
const char *native_expected_build_id();

#endif
