#ifndef NEST_MQTT_JSON_FLATTEN_H
#define NEST_MQTT_JSON_FLATTEN_H

#include <map>
#include <string>

bool extract_json_object(const std::string &text, std::string *json);
bool flatten_json(const std::string &json, std::map<std::string, std::string> *out,
                  std::string *error);
bool json_key_is_sensitive(const std::string &path);
std::string topic_component(const std::string &path);
std::string json_escape(const std::string &value);

#endif
