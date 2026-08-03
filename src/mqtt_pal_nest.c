#include <mqtt.h>

#include <errno.h>
#include <sys/socket.h>

ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle fd, const void *buf, size_t len, int flags) {
    size_t sent = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    while (sent < len) {
        ssize_t rc = send(fd, (const char *)buf + sent, len - sent, flags);
        if (rc > 0) {
            sent += (size_t)rc;
            continue;
        }
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        return sent ? (ssize_t)sent : MQTT_ERROR_SOCKET_ERROR;
    }
    return (ssize_t)sent;
}

ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle fd, void *buf, size_t bufsz, int flags) {
    size_t received = 0;
    while (received < bufsz) {
        ssize_t rc = recv(fd, (char *)buf + received, bufsz - received, flags);
        if (rc > 0) {
            received += (size_t)rc;
            continue;
        }
        if (rc == 0) return received ? (ssize_t)received : MQTT_ERROR_CONNECTION_CLOSED;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        return received ? (ssize_t)received : MQTT_ERROR_SOCKET_ERROR;
    }
    return (ssize_t)received;
}
