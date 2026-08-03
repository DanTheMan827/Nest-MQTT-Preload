#ifndef NEST_MQTT_PAL_H
#define NEST_MQTT_PAL_H

#include <arpa/inet.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

typedef int mqtt_pal_socket_handle;
typedef time_t mqtt_pal_time_t;
typedef pthread_mutex_t mqtt_pal_mutex_t;

#define MQTT_PAL_HTONS(value) htons(value)
#define MQTT_PAL_NTOHS(value) ntohs(value)
#define MQTT_PAL_TIME() time(NULL)

#define MQTT_PAL_MUTEX_INIT(mutex_ptr) pthread_mutex_init((mutex_ptr), NULL)
#define MQTT_PAL_MUTEX_LOCK(mutex_ptr) pthread_mutex_lock((mutex_ptr))
#define MQTT_PAL_MUTEX_UNLOCK(mutex_ptr) pthread_mutex_unlock((mutex_ptr))

ssize_t mqtt_pal_sendall(mqtt_pal_socket_handle fd, const void *buf, size_t len, int flags);
ssize_t mqtt_pal_recvall(mqtt_pal_socket_handle fd, void *buf, size_t bufsz, int flags);

#endif
