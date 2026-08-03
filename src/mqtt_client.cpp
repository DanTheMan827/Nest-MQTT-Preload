#include "mqtt_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <sstream>

namespace {

static uint64_t now_ms() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return static_cast<uint64_t>(tv.tv_sec) * 1000ULL + static_cast<uint64_t>(tv.tv_usec / 1000);
}

static void put_u16(std::vector<unsigned char> *out, uint16_t v) {
    out->push_back(static_cast<unsigned char>((v >> 8) & 0xff));
    out->push_back(static_cast<unsigned char>(v & 0xff));
}

static void put_string(std::vector<unsigned char> *out, const std::string &s) {
    size_t n = std::min<size_t>(s.size(), 65535);
    put_u16(out, static_cast<uint16_t>(n));
    out->insert(out->end(), s.begin(), s.begin() + n);
}

static void put_remaining_length(std::vector<unsigned char> *out, size_t length) {
    do {
        unsigned char encoded = static_cast<unsigned char>(length % 128);
        length /= 128;
        if (length > 0) encoded |= 0x80;
        out->push_back(encoded);
    } while (length > 0);
}

static bool decode_remaining_length(const std::vector<unsigned char> &in, size_t start,
                                    size_t *value, size_t *bytes) {
    size_t multiplier = 1;
    size_t result = 0;
    size_t used = 0;
    for (size_t i = start; i < in.size() && used < 4; ++i) {
        unsigned char b = in[i];
        result += (b & 127) * multiplier;
        ++used;
        if ((b & 128) == 0) {
            *value = result;
            *bytes = used;
            return true;
        }
        multiplier *= 128;
    }
    return false;
}

}  // namespace

MqttClient::MqttClient()
    : fd_(-1), handler_(0), packet_id_(1), last_tx_ms_(0), last_rx_ms_(0) {}

MqttClient::~MqttClient() { disconnect(); }

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
    if (flags >= 0) fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

    std::vector<unsigned char> body;
    put_string(&body, "MQTT");
    body.push_back(4);  // MQTT 3.1.1
    unsigned char connect_flags = 0x02;  // clean session
    connect_flags |= 0x04;  // Last Will present
    connect_flags |= 0x20;  // retain Last Will
    if (!cfg.mqtt_username.empty()) connect_flags |= 0x80;
    if (!cfg.mqtt_password.empty()) connect_flags |= 0x40;
    body.push_back(connect_flags);
    put_u16(&body, static_cast<uint16_t>(cfg.keepalive_seconds));
    put_string(&body, cfg.client_id);
    put_string(&body, cfg.base_topic + "/state/availability");
    put_string(&body, "offline");
    if (!cfg.mqtt_username.empty()) put_string(&body, cfg.mqtt_username);
    if (!cfg.mqtt_password.empty()) put_string(&body, cfg.mqtt_password);
    if (!send_packet(0x10, body)) {
        if (error) *error = "failed to send MQTT CONNECT";
        disconnect();
        return false;
    }

    uint64_t deadline = now_ms() + 5000;
    while (now_ms() < deadline) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        rc = select(fd_ + 1, &rfds, 0, 0, &tv);
        if (rc < 0 && errno != EINTR) break;
        if (rc > 0 && FD_ISSET(fd_, &rfds)) {
            unsigned char buf[64];
            ssize_t n = recv(fd_, buf, sizeof(buf), 0);
            if (n <= 0) break;
            rx_.insert(rx_.end(), buf, buf + n);
            if (rx_.size() >= 4 && rx_[0] == 0x20 && rx_[1] == 0x02) {
                unsigned char result = rx_[3];
                rx_.erase(rx_.begin(), rx_.begin() + 4);
                if (result == 0) {
                    last_rx_ms_ = last_tx_ms_ = now_ms();
                    return true;
                }
                if (error) {
                    std::ostringstream os;
                    os << "broker rejected CONNECT, return code " << static_cast<int>(result);
                    *error = os.str();
                }
                disconnect();
                return false;
            }
        }
    }
    if (error) *error = "timeout waiting for CONNACK";
    disconnect();
    return false;
}

void MqttClient::disconnect() {
    if (fd_ >= 0) {
        std::vector<unsigned char> empty;
        send_packet(0xe0, empty);
    }
    close_socket();
}

void MqttClient::close_socket() {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
    rx_.clear();
}

bool MqttClient::connected() const { return fd_ >= 0; }

bool MqttClient::send_all(const unsigned char *data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t n = send(fd_, data + sent, size - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EINTR)) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd_, &wfds);
            struct timeval tv;
            tv.tv_sec = 2;
            tv.tv_usec = 0;
            int rc = select(fd_ + 1, 0, &wfds, 0, &tv);
            if (rc > 0) continue;
        }
        close_socket();
        return false;
    }
    last_tx_ms_ = now_ms();
    return true;
}

bool MqttClient::send_packet(unsigned char header, const std::vector<unsigned char> &body) {
    if (fd_ < 0) return false;
    std::vector<unsigned char> packet;
    packet.reserve(body.size() + 5);
    packet.push_back(header);
    put_remaining_length(&packet, body.size());
    packet.insert(packet.end(), body.begin(), body.end());
    return send_all(&packet[0], packet.size());
}

bool MqttClient::publish(const std::string &topic, const std::string &payload, bool retain) {
    std::vector<unsigned char> body;
    put_string(&body, topic);
    body.insert(body.end(), payload.begin(), payload.end());
    return send_packet(static_cast<unsigned char>(0x30 | (retain ? 0x01 : 0x00)), body);
}

bool MqttClient::subscribe(const std::vector<std::string> &topics) {
    if (topics.empty()) return true;
    std::vector<unsigned char> body;
    uint16_t id = packet_id_++;
    if (packet_id_ == 0) packet_id_ = 1;
    put_u16(&body, id);
    for (size_t i = 0; i < topics.size(); ++i) {
        put_string(&body, topics[i]);
        body.push_back(0);  // QoS 0
    }
    return send_packet(0x82, body);
}

bool MqttClient::read_available() {
    unsigned char buf[4096];
    for (;;) {
        ssize_t n = recv(fd_, buf, sizeof(buf), 0);
        if (n > 0) {
            rx_.insert(rx_.end(), buf, buf + n);
            last_rx_ms_ = now_ms();
            continue;
        }
        if (n == 0) {
            close_socket();
            return false;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        close_socket();
        return false;
    }
}

bool MqttClient::handle_packet(unsigned char type_flags, const unsigned char *body, size_t size) {
    unsigned char type = type_flags >> 4;
    if (type == 3) {  // PUBLISH
        if (size < 2) return false;
        size_t topic_len = (static_cast<size_t>(body[0]) << 8) | body[1];
        if (topic_len + 2 > size) return false;
        std::string topic(reinterpret_cast<const char *>(body + 2), topic_len);
        size_t pos = 2 + topic_len;
        unsigned qos = (type_flags >> 1) & 3;
        if (qos > 0) {
            if (pos + 2 > size) return false;
            pos += 2;
        }
        std::string payload(reinterpret_cast<const char *>(body + pos), size - pos);
        if (handler_) handler_->on_mqtt_message(topic, payload);
    }
    return true;
}

bool MqttClient::parse_packets() {
    while (rx_.size() >= 2) {
        size_t remaining = 0;
        size_t length_bytes = 0;
        if (!decode_remaining_length(rx_, 1, &remaining, &length_bytes)) {
            if (rx_.size() > 5) {
                close_socket();
                return false;
            }
            return true;
        }
        size_t packet_size = 1 + length_bytes + remaining;
        if (rx_.size() < packet_size) return true;
        unsigned char header = rx_[0];
        const unsigned char *body = remaining ? &rx_[1 + length_bytes] : 0;
        if (!handle_packet(header, body, remaining)) {
            close_socket();
            return false;
        }
        rx_.erase(rx_.begin(), rx_.begin() + packet_size);
    }
    return true;
}

bool MqttClient::loop(int timeout_ms) {
    if (fd_ < 0) return false;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd_, &rfds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int rc = select(fd_ + 1, &rfds, 0, 0, &tv);
    if (rc < 0 && errno != EINTR) {
        close_socket();
        return false;
    }
    if (rc > 0 && FD_ISSET(fd_, &rfds)) {
        if (!read_available() || !parse_packets()) return false;
    }
    uint64_t now = now_ms();
    if (now - last_tx_ms_ >= static_cast<uint64_t>(cfg_.keepalive_seconds) * 500ULL) {
        std::vector<unsigned char> empty;
        if (!send_packet(0xc0, empty)) return false;
    }
    if (now - last_rx_ms_ > static_cast<uint64_t>(cfg_.keepalive_seconds) * 2000ULL) {
        close_socket();
        return false;
    }
    return true;
}
