#ifndef NEST_MQTT_CLIENT_H
#define NEST_MQTT_CLIENT_H

#include "config.h"

#include <stdint.h>
#include <string>
#include <vector>

extern "C" {
#include <mqtt.h>
}

class MqttMessageHandler {
public:
    virtual ~MqttMessageHandler() {}
    virtual void on_mqtt_message(const std::string &topic, const std::string &payload) = 0;
};

class MqttClient {
public:
    MqttClient();
    ~MqttClient();

    bool connect_to(const Config &cfg, MqttMessageHandler *handler, std::string *error);
    void disconnect();
    bool connected() const;
    bool publish(const std::string &topic, const std::string &payload, bool retain);
    bool subscribe(const std::vector<std::string> &topics);
    bool loop(int timeout_ms);

private:
    int fd_;
    bool initialized_;
    Config cfg_;
    MqttMessageHandler *handler_;
    struct mqtt_client client_;
    std::vector<unsigned char> send_buffer_;
    std::vector<unsigned char> recv_buffer_;

    static void receive_publish(void **state, struct mqtt_response_publish *published);
    bool sync(std::string *error);
    bool wait_for_connect(std::string *error);
    void close_socket();
};

#endif
