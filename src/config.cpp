#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <map>
#include <sstream>

namespace {

static std::string trim(const std::string &s) {
    size_t first = 0;
    while (first < s.size() && isspace(static_cast<unsigned char>(s[first]))) ++first;
    size_t last = s.size();
    while (last > first && isspace(static_cast<unsigned char>(s[last - 1]))) --last;
    return s.substr(first, last - first);
}

static bool parse_bool(const std::string &v, bool fallback) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s.push_back(static_cast<char>(tolower(static_cast<unsigned char>(v[i]))));
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return fallback;
}

static int parse_int(const std::string &v, int fallback, int minv, int maxv) {
    char *end = 0;
    errno = 0;
    long n = strtol(v.c_str(), &end, 10);
    if (errno || end == v.c_str() || *end != '\0' || n < minv || n > maxv) return fallback;
    return static_cast<int>(n);
}

static std::string getenv_string(const char *name) {
    const char *v = getenv(name);
    return v ? std::string(v) : std::string();
}

static void set_if_present(std::string *field, const std::map<std::string, std::string> &kv,
                           const char *key) {
    std::map<std::string, std::string>::const_iterator it = kv.find(key);
    if (it != kv.end()) *field = it->second;
}

static std::string hostname_string() {
    char buf[128];
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return std::string(buf);
    }
    return "nest";
}

static std::string safe_id(const std::string &in) {
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (isalnum(c) || c == '_' || c == '-') out.push_back(static_cast<char>(tolower(c)));
        else out.push_back('_');
    }
    if (out.empty()) out = "nest";
    return out;
}

}  // namespace

Config::Config()
    : config_path("/data/nest-mqtt.conf"),
      mqtt_host("127.0.0.1"),
      mqtt_port(1883),
      client_id(""),
      base_topic("nest/thermostat"),
      discovery_prefix("homeassistant"),
      device_name("Nest Thermostat"),
      device_id(""),
      keepalive_seconds(30),
      poll_seconds(5),
      retain_state(true),
      homeassistant_discovery(true),
      publish_raw_properties(true),
      native_writes(true),
      block_sleep(true) {}

bool load_config(Config *out, std::string *error) {
    if (!out) return false;
    Config cfg;
    std::string env_path = getenv_string("NEST_MQTT_CONFIG");
    if (!env_path.empty()) cfg.config_path = env_path;

    std::map<std::string, std::string> kv;
    std::ifstream f(cfg.config_path.c_str());
    if (f) {
        std::string line;
        int line_no = 0;
        while (std::getline(f, line)) {
            ++line_no;
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) {
                if (error) {
                    std::ostringstream os;
                    os << cfg.config_path << ':' << line_no << ": expected key=value";
                    *error = os.str();
                }
                return false;
            }
            std::string key = trim(line.substr(0, eq));
            std::string value = trim(line.substr(eq + 1));
            if (value.size() >= 2 && ((value[0] == '"' && value[value.size() - 1] == '"') ||
                                      (value[0] == '\'' && value[value.size() - 1] == '\''))) {
                value = value.substr(1, value.size() - 2);
            }
            kv[key] = value;
        }
    }

    set_if_present(&cfg.mqtt_host, kv, "mqtt_host");
    set_if_present(&cfg.mqtt_username, kv, "mqtt_username");
    set_if_present(&cfg.mqtt_password, kv, "mqtt_password");
    set_if_present(&cfg.client_id, kv, "client_id");
    set_if_present(&cfg.base_topic, kv, "base_topic");
    set_if_present(&cfg.discovery_prefix, kv, "discovery_prefix");
    set_if_present(&cfg.device_name, kv, "device_name");
    set_if_present(&cfg.device_id, kv, "device_id");

    if (kv.count("mqtt_port")) cfg.mqtt_port = parse_int(kv["mqtt_port"], cfg.mqtt_port, 1, 65535);
    if (kv.count("keepalive_seconds")) cfg.keepalive_seconds = parse_int(kv["keepalive_seconds"], cfg.keepalive_seconds, 5, 3600);
    if (kv.count("poll_seconds")) cfg.poll_seconds = parse_int(kv["poll_seconds"], cfg.poll_seconds, 1, 300);
    if (kv.count("retain_state")) cfg.retain_state = parse_bool(kv["retain_state"], cfg.retain_state);
    if (kv.count("homeassistant_discovery")) cfg.homeassistant_discovery = parse_bool(kv["homeassistant_discovery"], cfg.homeassistant_discovery);
    if (kv.count("publish_raw_properties")) cfg.publish_raw_properties = parse_bool(kv["publish_raw_properties"], cfg.publish_raw_properties);
    if (kv.count("native_writes")) cfg.native_writes = parse_bool(kv["native_writes"], cfg.native_writes);
    if (kv.count("block_sleep")) cfg.block_sleep = parse_bool(kv["block_sleep"], cfg.block_sleep);

    const char *env_names[][2] = {
        {"NEST_MQTT_HOST", "mqtt_host"},
        {"NEST_MQTT_USERNAME", "mqtt_username"},
        {"NEST_MQTT_PASSWORD", "mqtt_password"},
        {"NEST_MQTT_CLIENT_ID", "client_id"},
        {"NEST_MQTT_BASE_TOPIC", "base_topic"},
        {"NEST_MQTT_DISCOVERY_PREFIX", "discovery_prefix"},
        {"NEST_MQTT_DEVICE_NAME", "device_name"},
        {"NEST_MQTT_DEVICE_ID", "device_id"}
    };
    for (size_t i = 0; i < sizeof(env_names) / sizeof(env_names[0]); ++i) {
        std::string v = getenv_string(env_names[i][0]);
        if (v.empty()) continue;
        std::string key = env_names[i][1];
        if (key == "mqtt_host") cfg.mqtt_host = v;
        else if (key == "mqtt_username") cfg.mqtt_username = v;
        else if (key == "mqtt_password") cfg.mqtt_password = v;
        else if (key == "client_id") cfg.client_id = v;
        else if (key == "base_topic") cfg.base_topic = v;
        else if (key == "discovery_prefix") cfg.discovery_prefix = v;
        else if (key == "device_name") cfg.device_name = v;
        else if (key == "device_id") cfg.device_id = v;
    }

    std::string port_env = getenv_string("NEST_MQTT_PORT");
    if (!port_env.empty()) cfg.mqtt_port = parse_int(port_env, cfg.mqtt_port, 1, 65535);
    std::string writes_env = getenv_string("NEST_MQTT_NATIVE_WRITES");
    if (!writes_env.empty()) cfg.native_writes = parse_bool(writes_env, cfg.native_writes);
    std::string sleep_env = getenv_string("NEST_MQTT_BLOCK_SLEEP");
    if (!sleep_env.empty()) cfg.block_sleep = parse_bool(sleep_env, cfg.block_sleep);

    std::string host = hostname_string();
    if (cfg.device_id.empty()) cfg.device_id = safe_id(host);
    if (cfg.client_id.empty()) cfg.client_id = "nest-mqtt-" + cfg.device_id;
    while (!cfg.base_topic.empty() && cfg.base_topic[cfg.base_topic.size() - 1] == '/') cfg.base_topic.resize(cfg.base_topic.size() - 1);
    while (!cfg.discovery_prefix.empty() && cfg.discovery_prefix[cfg.discovery_prefix.size() - 1] == '/') cfg.discovery_prefix.resize(cfg.discovery_prefix.size() - 1);

    if (cfg.mqtt_host.empty() || cfg.base_topic.empty() || cfg.discovery_prefix.empty()) {
        if (error) *error = "mqtt_host, base_topic, and discovery_prefix must not be empty";
        return false;
    }
    *out = cfg;
    return true;
}

std::string config_example() {
    return
        "mqtt_host=192.168.1.10\n"
        "mqtt_port=1883\n"
        "mqtt_username=homeassistant\n"
        "mqtt_password=change-me\n"
        "base_topic=nest/thermostat\n"
        "discovery_prefix=homeassistant\n"
        "device_name=Nest Thermostat\n"
        "device_id=hallway_nest\n"
        "keepalive_seconds=30\n"
        "poll_seconds=5\n"
        "retain_state=true\n"
        "homeassistant_discovery=true\n"
        "publish_raw_properties=true\n"
        "native_writes=true\n"
        "block_sleep=true\n";
}
