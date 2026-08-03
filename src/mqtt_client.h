#ifndef NEST_MQTT_CLIENT_H
#define NEST_MQTT_CLIENT_H

#include "config.h"

#include <stdint.h>
#include <string>
#include <vector>

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
    Config cfg_;
    MqttMessageHandler *handler_;
    uint16_t packet_id_;
    uint64_t last_tx_ms_;
    uint64_t last_rx_ms_;
    std::vector<unsigned char> rx_;

    bool send_packet(unsigned char header, const std::vector<unsigned char> &body);
    bool send_all(const unsigned char *data, size_t size);
    bool read_available();
    bool parse_packets();
    bool handle_packet(unsigned char type_flags, const unsigned char *body, size_t size);
    void close_socket();
};

#endif
