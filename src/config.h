#ifndef NEST_MQTT_CONFIG_H
#define NEST_MQTT_CONFIG_H

#include <string>

struct Config {
    std::string config_path;
    std::string mqtt_host;
    int mqtt_port;
    std::string mqtt_username;
    std::string mqtt_password;
    std::string client_id;
    std::string base_topic;
    std::string discovery_prefix;
    std::string device_name;
    std::string device_id;
    int keepalive_seconds;
    int poll_seconds;
    bool retain_state;
    bool homeassistant_discovery;
    bool publish_raw_properties;
    bool native_writes;
    bool block_sleep;

    Config();
};

bool load_config(Config *out, std::string *error);
std::string config_example();

#endif
