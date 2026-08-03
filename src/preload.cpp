#include "bridge.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

namespace {

typedef void (*LogFn)(int, int, const char *, ...);
static LogFn g_original_log = 0;
static __thread bool g_in_log_hook = false;

static LogFn original_log() {
    if (!g_original_log) g_original_log = reinterpret_cast<LogFn>(dlsym(RTLD_NEXT, "nlLogWithComponent"));
    return g_original_log;
}

}  // namespace

extern "C" __attribute__((constructor, visibility("default"))) void nest_mqtt_preload_init() {
    g_in_log_hook = true;
    (void)original_log();
    g_in_log_hook = false;
    bridge_bootstrap();
}

extern "C" __attribute__((destructor, visibility("default"))) void nest_mqtt_preload_fini() {
    bridge_shutdown();
}

extern "C" __attribute__((visibility("default")))
void nlLogWithComponent(int level, int component, const char *format, ...) {
    const char *fmt = format ? format : "";
    char stack_buffer[1024];
    char *buffer = stack_buffer;
    size_t capacity = sizeof(stack_buffer);

    va_list ap;
    va_start(ap, format);
    va_list count_ap;
    va_copy(count_ap, ap);
    int needed = vsnprintf(stack_buffer, sizeof(stack_buffer), fmt, count_ap);
    va_end(count_ap);
    if (needed >= 0 && static_cast<size_t>(needed) >= sizeof(stack_buffer)) {
        capacity = static_cast<size_t>(needed) + 1;
        if (capacity > 131072) capacity = 131072;
        char *allocated = static_cast<char *>(malloc(capacity));
        if (allocated) {
            buffer = allocated;
            vsnprintf(buffer, capacity, fmt, ap);
        }
    }
    va_end(ap);

    if (!g_in_log_hook) {
        g_in_log_hook = true;
        bridge_ingest_log(buffer);
        g_in_log_hook = false;
    }

    LogFn fn = original_log();
    if (fn) fn(level, component, "%s", buffer);
    else {
        (void)write(STDERR_FILENO, buffer, strlen(buffer));
        (void)write(STDERR_FILENO, "\n", 1);
    }
    if (buffer != stack_buffer) free(buffer);
}

// Imported by nlclient. Interposing this final hardware sleep entry point is
// less invasive than patching the sleep manager state machine.
class nlWakeUp {
public:
    static void Sleep();
};

__attribute__((visibility("default"))) void nlWakeUp::Sleep() {
    if (bridge_should_block_sleep()) {
        bridge_sleep_blocked();
        return;
    }
    typedef void (*SleepFn)();
    static SleepFn original = 0;
    if (!original) original = reinterpret_cast<SleepFn>(dlsym(RTLD_NEXT, "_ZN8nlWakeUp5SleepEv"));
    if (original) original();
}
