#include "bridge.h"

#include "config.h"
#include "json_flatten.h"
#include "mqtt_client.h"
#include "native_nlclient.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <deque>
#include <map>
#include <sstream>
#include <vector>

namespace {

struct Publication {
    std::string topic;
    std::string payload;
    bool retain;
};

static uint64_t now_ms() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return static_cast<uint64_t>(tv.tv_sec) * 1000ULL + static_cast<uint64_t>(tv.tv_usec / 1000);
}

static void raw_log(const std::string &message) {
    std::string line = "[nest-mqtt] " + message + "\n";
    (void)write(STDERR_FILENO, line.data(), line.size());
}

static std::string number(double value, int precision) {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(precision);
    os << value;
    std::string s = os.str();
    while (s.size() > 1 && s.find('.') != std::string::npos && s[s.size() - 1] == '0') s.resize(s.size() - 1);
    if (!s.empty() && s[s.size() - 1] == '.') s.resize(s.size() - 1);
    return s;
}

static bool parse_number(const std::string &s, double *value) {
    char *end = 0;
    errno = 0;
    double v = strtod(s.c_str(), &end);
    if (errno || end == s.c_str() || *end != '\0') return false;
    *value = v;
    return true;
}

static bool parse_boolean(const std::string &s, bool *value) {
    if (s == "true" || s == "1" || s == "on" || s == "ON") { *value = true; return true; }
    if (s == "false" || s == "0" || s == "off" || s == "OFF") { *value = false; return true; }
    return false;
}

static std::string leaf_name(const std::string &path) {
    size_t dot = path.find_last_of('.');
    std::string leaf = dot == std::string::npos ? path : path.substr(dot + 1);
    size_t bracket = leaf.find('[');
    if (bracket != std::string::npos) leaf.resize(bracket);
    return leaf;
}

static double normalize_temperature(double value) {
    if (value > 1000.0 || value < -1000.0) return value / 65536.0;
    return value;
}

static std::string mode_from_native(int mode) {
    // Mapping inferred from the switch-over mode paths in this exact build.
    // Mode 4 additionally uses the emergency-heat flag.
    switch (mode) {
        case 0: return "heat";
        case 1: return "cool";
        case 2: return "auto";
        case 3: return "off";
        case 4: return "heat";
        default: return "off";
    }
}

static int native_from_mode(const std::string &mode) {
    if (mode == "heat") return 0;
    if (mode == "cool") return 1;
    if (mode == "heat_cool" || mode == "auto") return 2;
    if (mode == "off") return 3;
    return -1;
}

class Bridge : public MqttMessageHandler {
public:
    Bridge()
        : started_(false), stopping_(false), block_sleep_(true), thread_running_(false),
          next_native_poll_(0), reconnect_after_(0), reconnect_seconds_(1) {
        pthread_mutex_init(&lock_, 0);
        pthread_cond_init(&cond_, 0);
    }

    ~Bridge() {
        stop();
        pthread_cond_destroy(&cond_);
        pthread_mutex_destroy(&lock_);
    }

    void start() {
        pthread_mutex_lock(&lock_);
        if (started_) { pthread_mutex_unlock(&lock_); return; }
        std::string error;
        if (!load_config(&cfg_, &error)) {
            raw_log("configuration error: " + error + "; using built-in defaults");
            cfg_ = Config();
        }
        block_sleep_ = cfg_.block_sleep;
        std::string native_status;
        native_initialize(&native_status);
        raw_log(native_status);
        started_ = true;
        stopping_ = false;
        int rc = pthread_create(&thread_, 0, &Bridge::thread_entry, this);
        if (rc == 0) thread_running_ = true;
        else raw_log(std::string("pthread_create failed: ") + strerror(rc));
        pthread_mutex_unlock(&lock_);
    }

    void stop() {
        pthread_mutex_lock(&lock_);
        if (!started_) { pthread_mutex_unlock(&lock_); return; }
        stopping_ = true;
        pthread_cond_broadcast(&cond_);
        bool join = thread_running_;
        pthread_t t = thread_;
        pthread_mutex_unlock(&lock_);
        if (join && !pthread_equal(pthread_self(), t)) pthread_join(t, 0);
        pthread_mutex_lock(&lock_);
        thread_running_ = false;
        started_ = false;
        pthread_mutex_unlock(&lock_);
    }

    bool block_sleep() {
        pthread_mutex_lock(&lock_);
        bool value = block_sleep_;
        pthread_mutex_unlock(&lock_);
        return value;
    }

    void ingest_log(const char *line) {
        if (!line) return;
        pthread_mutex_lock(&lock_);
        if (started_ && !stopping_) {
            if (log_queue_.size() >= 512) log_queue_.pop_front();
            log_queue_.push_back(line);
            pthread_cond_signal(&cond_);
        }
        pthread_mutex_unlock(&lock_);
    }

    void sleep_blocked() {
        enqueue(cfg_.base_topic + "/event/sleep_blocked", number(static_cast<double>(time(0)), 0), false);
        enqueue(cfg_.base_topic + "/state/availability", "online", true);
    }

    virtual void on_mqtt_message(const std::string &topic, const std::string &payload) {
        if (topic == cfg_.discovery_prefix + "/status" && payload == "online") {
            if (cfg_.homeassistant_discovery && mqtt_.connected()) publish_discovery();
            return;
        }
        const std::string prefix = cfg_.base_topic + "/set/";
        if (topic.compare(0, prefix.size(), prefix) != 0) return;
        std::string command = topic.substr(prefix.size());
        std::string error;
        bool ok = false;
        double value = 0;
        if (!cfg_.native_writes) {
            error = "native_writes=false";
        } else if (command == "target_temperature" && parse_number(payload, &value)) {
            ok = native_command(NATIVE_SET_TARGET, value, &error);
        } else if (command == "target_temperature_low" && parse_number(payload, &value)) {
            ok = native_command(NATIVE_SET_TARGET_LOW, value, &error);
        } else if (command == "target_temperature_high" && parse_number(payload, &value)) {
            ok = native_command(NATIVE_SET_TARGET_HIGH, value, &error);
        } else if (command == "hvac_mode") {
            int mode = native_from_mode(payload);
            if (mode >= 0) ok = native_command(NATIVE_SET_MODE, mode, &error);
            else error = "unsupported HVAC mode";
        } else if (command == "emergency_heat") {
            bool enabled = false;
            if (parse_boolean(payload, &enabled)) ok = native_command(NATIVE_SET_EMERGENCY_HEAT, enabled ? 1 : 0, &error);
            else error = "expected boolean payload";
        } else if (command == "preset_mode") {
            if (payload == "eco") ok = native_command(NATIVE_SET_ECO, 1, &error);
            else if (payload == "none") ok = native_command(NATIVE_SET_ECO, 0, &error);
            else error = "supported presets are eco and none";
        } else {
            error = "unknown command or invalid numeric payload";
        }
        enqueue(cfg_.base_topic + "/event/last_command",
                std::string("{\"command\":\"") + json_escape(command) +
                "\",\"ok\":" + (ok ? "true" : "false") +
                ",\"detail\":\"" + json_escape(ok ? "accepted" : error) + "\"}", false);
        if (!ok) raw_log("command " + command + " rejected: " + error);
    }

private:
    Config cfg_;
    pthread_mutex_t lock_;
    pthread_cond_t cond_;
    pthread_t thread_;
    bool started_;
    bool stopping_;
    bool block_sleep_;
    bool thread_running_;
    std::deque<std::string> log_queue_;
    std::deque<Publication> publish_queue_;
    MqttClient mqtt_;
    uint64_t next_native_poll_;
    uint64_t reconnect_after_;
    int reconnect_seconds_;
    std::map<std::string, std::string> last_state_;

    static void *thread_entry(void *arg) {
        static_cast<Bridge *>(arg)->run();
        return 0;
    }

    bool stopping() {
        pthread_mutex_lock(&lock_);
        bool value = stopping_;
        pthread_mutex_unlock(&lock_);
        return value;
    }

    void enqueue(const std::string &topic, const std::string &payload, bool retain) {
        pthread_mutex_lock(&lock_);
        if (publish_queue_.size() >= 1024) publish_queue_.pop_front();
        Publication p;
        p.topic = topic;
        p.payload = payload;
        p.retain = retain;
        publish_queue_.push_back(p);
        pthread_cond_signal(&cond_);
        pthread_mutex_unlock(&lock_);
    }

    void publish_state(const std::string &name, const std::string &value) {
        if (value.empty()) return;
        if (last_state_[name] == value) return;
        last_state_[name] = value;
        enqueue(cfg_.base_topic + "/state/" + name, value, cfg_.retain_state);
    }

    void publish_discovery() {
        const std::string object = cfg_.device_id;
        const std::string device = std::string("\"device\":{\"identifiers\":[\"") +
            json_escape(cfg_.device_id) + "\"],\"name\":\"" + json_escape(cfg_.device_name) +
            "\",\"manufacturer\":\"Nest Labs\",\"model\":\"Learning Thermostat\"}";
        const std::string origin = std::string("\"origin\":{\"name\":\"nest-mqtt-preload\",\"sw_version\":\"") + NEST_MQTT_VERSION + "\"}";
        std::ostringstream climate;
        climate << '{'
            << "\"name\":\"" << json_escape(cfg_.device_name) << "\"," 
            << "\"unique_id\":\"" << json_escape(object + "_climate") << "\"," 
            << device << ',' << origin << ','
            << "\"availability_topic\":\"" << json_escape(cfg_.base_topic + "/state/availability") << "\"," 
            << "\"current_temperature_topic\":\"" << json_escape(cfg_.base_topic + "/state/current_temperature") << "\"," 
            << "\"current_humidity_topic\":\"" << json_escape(cfg_.base_topic + "/state/current_humidity") << "\"," 
            << "\"temperature_state_topic\":\"" << json_escape(cfg_.base_topic + "/state/target_temperature") << "\"," 
            << "\"temperature_command_topic\":\"" << json_escape(cfg_.base_topic + "/set/target_temperature") << "\"," 
            << "\"temperature_low_state_topic\":\"" << json_escape(cfg_.base_topic + "/state/target_temperature_low") << "\"," 
            << "\"temperature_low_command_topic\":\"" << json_escape(cfg_.base_topic + "/set/target_temperature_low") << "\"," 
            << "\"temperature_high_state_topic\":\"" << json_escape(cfg_.base_topic + "/state/target_temperature_high") << "\"," 
            << "\"temperature_high_command_topic\":\"" << json_escape(cfg_.base_topic + "/set/target_temperature_high") << "\"," 
            << "\"mode_state_topic\":\"" << json_escape(cfg_.base_topic + "/state/hvac_mode") << "\"," 
            << "\"mode_command_topic\":\"" << json_escape(cfg_.base_topic + "/set/hvac_mode") << "\"," 
            << "\"action_topic\":\"" << json_escape(cfg_.base_topic + "/state/hvac_action") << "\"," 
            << "\"preset_mode_state_topic\":\"" << json_escape(cfg_.base_topic + "/state/preset_mode") << "\"," 
            << "\"preset_mode_command_topic\":\"" << json_escape(cfg_.base_topic + "/set/preset_mode") << "\"," 
            << "\"modes\":[\"off\",\"heat\",\"cool\",\"auto\"],"
            << "\"preset_modes\":[\"none\",\"eco\"],"
            << "\"temperature_unit\":\"C\",\"min_temp\":7,\"max_temp\":32,\"temp_step\":0.5} ";
        enqueue(cfg_.discovery_prefix + "/climate/" + object + "/config", climate.str(), true);

        std::string battery = std::string("{\"name\":\"Battery level\",\"unique_id\":\"") +
            json_escape(object + "_battery") + "\",\"state_topic\":\"" +
            json_escape(cfg_.base_topic + "/state/battery_level") + "\"," + device + ',' + origin + '}';
        enqueue(cfg_.discovery_prefix + "/sensor/" + object + "_battery/config", battery, true);
    }

    void process_log(const std::string &line) {
        if (line.find("nlCZBucket:") == std::string::npos &&
            line.find("Smart Thermostat Manager:") == std::string::npos) return;
        std::string json;
        if (!extract_json_object(line, &json)) return;
        std::map<std::string, std::string> fields;
        std::string error;
        if (!flatten_json(json, &fields, &error)) return;
        bool heater = false, cooler = false, fan = false;
        bool heater_seen = false, cooler_seen = false, fan_seen = false;
        for (std::map<std::string, std::string>::const_iterator it = fields.begin(); it != fields.end(); ++it) {
            if (json_key_is_sensitive(it->first)) continue;
            const std::string leaf = leaf_name(it->first);
            const std::string &value = it->second;
            if (cfg_.publish_raw_properties) {
                enqueue(cfg_.base_topic + "/property/" + topic_component(it->first), value, cfg_.retain_state);
            }
            double numeric = 0;
            if ((leaf == "current_temperature" || leaf == "backplate_temperature") && parse_number(value, &numeric))
                publish_state("current_temperature", number(normalize_temperature(numeric), 3));
            else if (leaf == "target_temperature" && parse_number(value, &numeric))
                publish_state("target_temperature", number(normalize_temperature(numeric), 3));
            else if ((leaf == "target_temperature_low" || leaf == "away_temperature_low") && parse_number(value, &numeric))
                publish_state("target_temperature_low", number(normalize_temperature(numeric), 3));
            else if ((leaf == "target_temperature_high" || leaf == "away_temperature_high") && parse_number(value, &numeric))
                publish_state("target_temperature_high", number(normalize_temperature(numeric), 3));
            else if ((leaf == "current_humidity" || leaf == "humidity") && parse_number(value, &numeric))
                publish_state("current_humidity", number(numeric, 1));
            else if (leaf == "battery_level" && parse_number(value, &numeric))
                publish_state("battery_level", number(numeric, 3));
            else if (leaf == "target_temperature_type" || leaf == "current_schedule_mode") {
                if (parse_number(value, &numeric)) publish_state("hvac_mode", mode_from_native(static_cast<int>(numeric)));
                else if (value == "range") publish_state("hvac_mode", "auto");
                else if (value == "heat" || value == "cool" || value == "off" || value == "auto" || value == "heat_cool") publish_state("hvac_mode", value == "heat_cool" ? "auto" : value);
            } else if (leaf == "eco" || leaf == "manual_eco_all") {
                bool enabled = false;
                if (parse_boolean(value, &enabled)) publish_state("preset_mode", enabled ? "eco" : "none");
                else if (parse_number(value, &numeric)) publish_state("preset_mode", numeric != 0 ? "eco" : "none");
            } else if (leaf == "hvac_heater_state") heater_seen = parse_boolean(value, &heater);
            else if (leaf == "hvac_ac_state") cooler_seen = parse_boolean(value, &cooler);
            else if (leaf == "hvac_fan_state") fan_seen = parse_boolean(value, &fan);
        }
        if (heater_seen || cooler_seen || fan_seen) {
            if (heater) publish_state("hvac_action", "heating");
            else if (cooler) publish_state("hvac_action", "cooling");
            else if (fan) publish_state("hvac_action", "fan");
            else publish_state("hvac_action", "idle");
        }
    }

    void poll_native() {
        NativeSnapshot s;
        if (!native_poll(&s)) return;
        if (s.current_temperature_valid) publish_state("current_temperature", number(s.current_temperature_c, 3));
        if (s.target_temperature_valid) publish_state("target_temperature", number(s.target_temperature_c, 3));
        if (s.target_low_valid) publish_state("target_temperature_low", number(s.target_low_c, 3));
        if (s.target_high_valid) publish_state("target_temperature_high", number(s.target_high_c, 3));
        if (s.battery_valid) publish_state("battery_level", number(s.battery_level, 3));
        if (s.mode_valid) publish_state("hvac_mode", mode_from_native(s.native_mode));
        if (s.emergency_heat_valid) publish_state("emergency_heat", s.emergency_heat ? "true" : "false");
        if (s.eco_valid) publish_state("preset_mode", s.eco_mode == 2 ? "eco" : "none");
    }

    void drain_logs() {
        std::deque<std::string> local;
        pthread_mutex_lock(&lock_);
        local.swap(log_queue_);
        pthread_mutex_unlock(&lock_);
        while (!local.empty()) {
            process_log(local.front());
            local.pop_front();
        }
    }

    bool drain_publications() {
        for (;;) {
            Publication p;
            bool have = false;
            pthread_mutex_lock(&lock_);
            if (!publish_queue_.empty()) {
                p = publish_queue_.front();
                publish_queue_.pop_front();
                have = true;
            }
            pthread_mutex_unlock(&lock_);
            if (!have) return true;
            if (!mqtt_.publish(p.topic, p.payload, p.retain)) {
                pthread_mutex_lock(&lock_);
                publish_queue_.push_front(p);
                pthread_mutex_unlock(&lock_);
                return false;
            }
        }
    }

    void run() {
        next_native_poll_ = now_ms();
        while (!stopping()) {
            drain_logs();
            uint64_t now = now_ms();
            if (now >= next_native_poll_) {
                poll_native();
                next_native_poll_ = now + static_cast<uint64_t>(cfg_.poll_seconds) * 1000ULL;
            }
            if (!mqtt_.connected()) {
                if (now >= reconnect_after_) {
                    std::string error;
                    if (mqtt_.connect_to(cfg_, this, &error)) {
                        raw_log("connected to MQTT broker " + cfg_.mqtt_host);
                        reconnect_seconds_ = 1;
                        std::vector<std::string> topics;
                        topics.push_back(cfg_.base_topic + "/set/target_temperature");
                        topics.push_back(cfg_.base_topic + "/set/target_temperature_low");
                        topics.push_back(cfg_.base_topic + "/set/target_temperature_high");
                        topics.push_back(cfg_.base_topic + "/set/hvac_mode");
                        topics.push_back(cfg_.base_topic + "/set/emergency_heat");
                        topics.push_back(cfg_.base_topic + "/set/preset_mode");
                        topics.push_back(cfg_.discovery_prefix + "/status");
                        mqtt_.subscribe(topics);
                        if (cfg_.homeassistant_discovery) publish_discovery();
                        mqtt_.publish(cfg_.base_topic + "/state/availability", "online", true);
                    } else {
                        raw_log("MQTT connect failed: " + error);
                        reconnect_after_ = now + static_cast<uint64_t>(reconnect_seconds_) * 1000ULL;
                        if (reconnect_seconds_ < 60) reconnect_seconds_ *= 2;
                    }
                }
                usleep(200000);
                continue;
            }
            if (!drain_publications() || !mqtt_.loop(200)) {
                raw_log("MQTT connection lost");
                reconnect_after_ = now_ms() + 1000;
            }
        }
        if (mqtt_.connected()) mqtt_.publish(cfg_.base_topic + "/state/availability", "offline", true);
        mqtt_.disconnect();
    }
};

static Bridge &bridge() {
    static Bridge instance;
    return instance;
}

}  // namespace

void bridge_bootstrap() { bridge().start(); }
void bridge_shutdown() { bridge().stop(); }
void bridge_ingest_log(const char *line) { bridge().ingest_log(line); }
bool bridge_should_block_sleep() { return bridge().block_sleep(); }
void bridge_sleep_blocked() { bridge().sleep_blocked(); }
