#include "mqtt_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <sstream>

namespace {

static uint64_t now_ms() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
           static_cast<uint64_t>(tv.tv_usec / 1000);
}

static std::string mqtt_error(enum MQTTErrors error) {
    const char *text = mqtt_error_str(error);
    return text ? text : "unknown MQTT-C error";
}

}  // namespace

MqttClient::MqttClient()
    : fd_(-1), initialized_(false), handler_(0),
      send_buffer_(16384), recv_buffer_(16384) {
    memset(&client_, 0, sizeof(client_));
}

MqttClient::~MqttClient() {
    disconnect();
    if (initialized_) pthread_mutex_destroy(&client_.mutex);
}

void MqttClient::receive_publish(void **state, struct mqtt_response_publish *published) {
    if (!state || !*state || !published) return;
    MqttClient *self = static_cast<MqttClient *>(*state);
    if (!self->handler_) return;

    const char *topic_data = static_cast<const char *>(published->topic_name);
    const char *payload_data = static_cast<const char *>(published->application_message);
    std::string topic(topic_data ? topic_data : "", published->topic_name_size);
    std::string payload(payload_data ? payload_data : "", published->application_message_size);
    self->handler_->on_mqtt_message(topic, payload);
}

bool MqttClient::connect_to(const Config &cfg, MqttMessageHandler *handler, std::string *error) {
    disconnect();
    cfg_ = cfg;
    handler_ = handler;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port[16];
    snprintf(port, sizeof(port), "%d", cfg.mqtt_port);
    struct addrinfo *res = 0;
    int rc = getaddrinfo(cfg.mqtt_host.c_str(), port, &hints, &res);
    if (rc != 0) {
        if (error) *error = gai_strerror(rc);
        return false;
    }

    for (struct addrinfo *p = res; p; p = p->ai_next) {
        int fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            fd_ = fd;
            break;
        }
        close(fd);
    }
    freeaddrinfo(res);

    if (fd_ < 0) {
        if (error) *error = std::string("connect: ") + strerror(errno);
        return false;
    }

    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        if (error) *error = std::string("fcntl: ") + strerror(errno);
        close_socket();
        return false;
    }

    if (initialized_) {
        pthread_mutex_destroy(&client_.mutex);
        memset(&client_, 0, sizeof(client_));
    }
    enum MQTTErrors init_result = mqtt_init(
        &client_, fd_, &send_buffer_[0], send_buffer_.size(),
        &recv_buffer_[0], recv_buffer_.size(), &MqttClient::receive_publish);
    initialized_ = true;
    client_.publish_response_callback_state = this;

    if (init_result != MQTT_OK && init_result != MQTT_ERROR_CONNECT_NOT_CALLED) {
        if (error) *error = "mqtt_init: " + mqtt_error(init_result);
        close_socket();
        return false;
    }

    const std::string will_topic = cfg.base_topic + "/state/availability";
    const char *username = cfg.mqtt_username.empty() ? 0 : cfg.mqtt_username.c_str();
    const char *password = cfg.mqtt_password.empty() ? 0 : cfg.mqtt_password.c_str();
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION | MQTT_CONNECT_WILL_RETAIN;

    enum MQTTErrors connect_result = mqtt_connect(
        &client_, cfg.client_id.c_str(), will_topic.c_str(), "offline", 7,
        username, password, connect_flags,
        static_cast<uint16_t>(cfg.keepalive_seconds));
    if (connect_result != MQTT_OK) {
        if (error) *error = "mqtt_connect: " + mqtt_error(connect_result);
        close_socket();
        return false;
    }

    return wait_for_connect(error);
}

bool MqttClient::wait_for_connect(std::string *error) {
    const uint64_t deadline = now_ms() + 5000;
    while (now_ms() < deadline) {
        if (!sync(error)) return false;

        /* MQTT-C keeps CONNECT queued until CONNACK completes it. */
        if (mqtt_mq_length(&client_.mq) == 0 && client_.error == MQTT_OK) return true;

        fd_set rfds;
        fd_set wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(fd_, &rfds);
        FD_SET(fd_, &wfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        int rc = select(fd_ + 1, &rfds, &wfds, 0, &tv);
        if (rc < 0 && errno != EINTR) {
            if (error) *error = std::string("select: ") + strerror(errno);
            close_socket();
            return false;
        }
    }

    if (error) *error = "timeout waiting for MQTT CONNACK";
    close_socket();
    return false;
}

bool MqttClient::sync(std::string *error) {
    if (fd_ < 0) return false;
    enum MQTTErrors rc = mqtt_sync(&client_);
    if (rc == MQTT_OK && client_.error == MQTT_OK) {
        mqtt_mq_clean(&client_.mq);
        return true;
    }

    enum MQTTErrors actual = client_.error == MQTT_OK ? rc : client_.error;
    if (error) *error = mqtt_error(actual);
    close_socket();
    return false;
}

void MqttClient::disconnect() {
    if (fd_ >= 0 && initialized_ && client_.error == MQTT_OK) {
        (void)mqtt_disconnect(&client_);
        for (int i = 0; i < 3; ++i) {
            if (mqtt_sync(&client_) != MQTT_OK) break;
            mqtt_mq_clean(&client_.mq);
            if (mqtt_mq_length(&client_.mq) == 0) break;
            usleep(10000);
        }
    }
    close_socket();
}

void MqttClient::close_socket() {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
}

bool MqttClient::connected() const {
    return fd_ >= 0 && initialized_ && client_.error == MQTT_OK;
}

bool MqttClient::publish(const std::string &topic, const std::string &payload, bool retain) {
    if (!connected()) return false;
    uint8_t flags = MQTT_PUBLISH_QOS_0;
    if (retain) flags |= MQTT_PUBLISH_RETAIN;
    enum MQTTErrors rc = mqtt_publish(&client_, topic.c_str(), payload.data(), payload.size(), flags);
    if (rc != MQTT_OK) {
        close_socket();
        return false;
    }
    return true;
}

bool MqttClient::subscribe(const std::vector<std::string> &topics) {
    if (!connected()) return false;
    for (size_t i = 0; i < topics.size(); ++i) {
        enum MQTTErrors rc = mqtt_subscribe(&client_, topics[i].c_str(), 0);
        if (rc != MQTT_OK) {
            close_socket();
            return false;
        }
    }
    return sync(0);
}

bool MqttClient::loop(int timeout_ms) {
    if (!connected()) return false;
    if (!sync(0)) return false;

    fd_set rfds;
    fd_set wfds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_SET(fd_, &rfds);
    if (mqtt_mq_length(&client_.mq) != 0) FD_SET(fd_, &wfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int rc = select(fd_ + 1, &rfds, &wfds, 0, &tv);
    if (rc < 0 && errno != EINTR) {
        close_socket();
        return false;
    }
    return sync(0);
}
