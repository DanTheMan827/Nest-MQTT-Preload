#include "native_nlclient.h"
#include "arm_hook.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <vector>

namespace {

static const char kExpectedBuildId[] = "31ee6d0af8d98780f53872ab5e729cbd5243bff3";
static const uintptr_t kSharedBucketCtor = 0x0009a6c4u;
static const uintptr_t kDeviceBucketCtor = 0x0007e4fcu;
static const uintptr_t kSharedReset = 0x0009ad48u;
static const uintptr_t kSharedAfterApply = 0x0009b0e8u;

static volatile uint32_t *g_shared_bucket = 0;
static volatile uint32_t *g_device_bucket = 0;
static bool g_hooks_active = false;
static pthread_mutex_t g_native_lock = PTHREAD_MUTEX_INITIALIZER;

typedef void *(*CtorFn)(void *);
static CtorFn g_shared_ctor_original = 0;
static CtorFn g_device_ctor_original = 0;

static void *shared_ctor_hook(void *self) {
    void *result = g_shared_ctor_original ? g_shared_ctor_original(self) : self;
    g_shared_bucket = reinterpret_cast<volatile uint32_t *>(self);
    return result;
}

static void *device_ctor_hook(void *self) {
    void *result = g_device_ctor_original ? g_device_ctor_original(self) : self;
    g_device_bucket = reinterpret_cast<volatile uint32_t *>(self);
    return result;
}

static std::string hex_bytes(const unsigned char *data, size_t size) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(hex[(data[i] >> 4) & 0xf]);
        out.push_back(hex[data[i] & 0xf]);
    }
    return out;
}

static size_t align4(size_t n) { return (n + 3u) & ~3u; }

static bool read_build_id(std::string *out) {
    std::ifstream f("/proc/self/exe", std::ios::binary);
    if (!f) return false;
    Elf32_Ehdr eh;
    f.read(reinterpret_cast<char *>(&eh), sizeof(eh));
    if (!f || memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS32) return false;
    f.seekg(eh.e_phoff, std::ios::beg);
    std::vector<Elf32_Phdr> ph(eh.e_phnum);
    if (!ph.empty()) f.read(reinterpret_cast<char *>(&ph[0]), ph.size() * sizeof(ph[0]));
    if (!f) return false;
    for (size_t i = 0; i < ph.size(); ++i) {
        if (ph[i].p_type != PT_NOTE || ph[i].p_filesz == 0 || ph[i].p_filesz > 1024 * 1024) continue;
        std::vector<unsigned char> note(ph[i].p_filesz);
        f.seekg(ph[i].p_offset, std::ios::beg);
        f.read(reinterpret_cast<char *>(&note[0]), note.size());
        if (!f) return false;
        size_t p = 0;
        while (p + 12 <= note.size()) {
            uint32_t namesz = 0, descsz = 0, type = 0;
            memcpy(&namesz, &note[p], 4);
            memcpy(&descsz, &note[p + 4], 4);
            memcpy(&type, &note[p + 8], 4);
            p += 12;
            if (p + align4(namesz) + align4(descsz) > note.size()) break;
            const unsigned char *name = &note[p];
            p += align4(namesz);
            const unsigned char *desc = &note[p];
            p += align4(descsz);
            if (type == NT_GNU_BUILD_ID && namesz >= 3 && memcmp(name, "GNU", 3) == 0) {
                *out = hex_bytes(desc, descsz);
                return true;
            }
        }
    }
    return false;
}

static int32_t q16(double value) {
    if (value > 32767.0) value = 32767.0;
    if (value < -32768.0) value = -32768.0;
    return static_cast<int32_t>(value * 65536.0 + (value >= 0 ? 0.5 : -0.5));
}

static double from_q16(int32_t value) { return static_cast<double>(value) / 65536.0; }

static uint32_t *client_object() {
    volatile uint32_t *bucket = g_shared_bucket;
    if (!bucket) return 0;
    return reinterpret_cast<uint32_t *>(bucket[1]);
}

static uint32_t vtable_entry(uint32_t *object, size_t byte_offset) {
    if (!object || !object[0]) return 0;
    uint32_t *vtable = reinterpret_cast<uint32_t *>(object[0]);
    return vtable[byte_offset / 4];
}

static int call_getter(uint32_t *object, size_t byte_offset, bool *ok) {
    uint32_t address = vtable_entry(object, byte_offset);
    if (!address) {
        if (ok) *ok = false;
        return 0;
    }
    typedef int (*Fn)(void *);
    if (ok) *ok = true;
    return reinterpret_cast<Fn>(address)(object);
}

static void (*resolve_apply_handler())(int *) {
    volatile uint32_t *bucket = g_shared_bucket;
    if (!bucket || !bucket[0]) return 0;
    uint32_t *vtable = reinterpret_cast<uint32_t *>(bucket[0]);
    for (size_t i = 0; i < 160; ++i) {
        if (vtable[i] != kSharedReset) continue;
        for (size_t j = i + 2; j <= i + 5 && j < 160; ++j) {
            if (vtable[j] == kSharedAfterApply && vtable[i + 1]) {
                return reinterpret_cast<void (*)(int *)>(vtable[i + 1]);
            }
        }
    }
    return 0;
}

static bool apply_staged(std::string *error) {
    void (*handler)(int *) = resolve_apply_handler();
    if (!handler) {
        if (error) *error = "could not locate nlCZBucket cloud-apply vtable slot";
        return false;
    }
    handler(reinterpret_cast<int *>(const_cast<uint32_t *>(g_shared_bucket)));
    return true;
}

}  // namespace

NativeSnapshot::NativeSnapshot()
    : valid(false), current_temperature_valid(false), current_temperature_c(0),
      target_temperature_valid(false), target_temperature_c(0), target_low_valid(false),
      target_low_c(0), target_high_valid(false), target_high_c(0), battery_valid(false),
      battery_level(0), mode_valid(false), native_mode(0), emergency_heat_valid(false),
      emergency_heat(false), eco_valid(false), eco_mode(0) {}

const char *native_expected_build_id() { return kExpectedBuildId; }

bool native_initialize(std::string *status) {
#if !defined(__arm__)
    if (status) *status = "passive mode: not an ARM build";
    return false;
#else
    std::string actual;
    if (!read_build_id(&actual)) {
        if (status) *status = "passive mode: unable to read /proc/self/exe GNU build ID";
        return false;
    }
    if (actual != kExpectedBuildId) {
        if (status) *status = "passive mode: unsupported nlclient build ID " + actual;
        return false;
    }
    char error[160];
    error[0] = '\0';
    void *shared_trampoline = 0;
    if (!arm_install_hook(reinterpret_cast<void *>(kSharedBucketCtor),
                          reinterpret_cast<void *>(&shared_ctor_hook),
                          &shared_trampoline, error, sizeof(error))) {
        if (status) *status = std::string("passive mode: shared bucket hook failed: ") + error;
        return false;
    }
    g_shared_ctor_original = reinterpret_cast<CtorFn>(shared_trampoline);

    void *device_trampoline = 0;
    if (!arm_install_hook(reinterpret_cast<void *>(kDeviceBucketCtor),
                          reinterpret_cast<void *>(&device_ctor_hook),
                          &device_trampoline, error, sizeof(error))) {
        if (status) *status = std::string("partial native mode: device bucket hook failed: ") + error;
        g_hooks_active = true;
        return true;
    }
    g_device_ctor_original = reinterpret_cast<CtorFn>(device_trampoline);
    g_hooks_active = true;
    if (status) *status = "native mode enabled for nlclient build " + actual;
    return true;
#endif
}

bool native_hooks_active() { return g_hooks_active; }

bool native_poll(NativeSnapshot *snapshot) {
    if (!snapshot) return false;
    *snapshot = NativeSnapshot();
    pthread_mutex_lock(&g_native_lock);
    volatile uint32_t *shared = g_shared_bucket;
    volatile uint32_t *device = g_device_bucket;
    uint32_t *client = client_object();
    if (shared) {
        snapshot->target_temperature_valid = true;
        snapshot->target_temperature_c = from_q16(static_cast<int32_t>(shared[0x58]));  // +0x160
    }
    if (device && device[0x45] != 0) {  // +0x114 valid
        snapshot->current_temperature_valid = true;
        snapshot->current_temperature_c = from_q16(static_cast<int32_t>(device[0x40])); // +0x100
        snapshot->battery_valid = true;
        snapshot->battery_level = static_cast<double>(static_cast<int32_t>(device[0x44])); // +0x110
    }
    if (client) {
        bool ok = false;
        int value = call_getter(client, 0xc8, &ok);
        if (ok) { snapshot->target_low_valid = true; snapshot->target_low_c = from_q16(value); }
        value = call_getter(client, 0xc0, &ok);
        if (ok) { snapshot->target_high_valid = true; snapshot->target_high_c = from_q16(value); }
        value = call_getter(client, 0x10c, &ok);
        if (ok) { snapshot->mode_valid = true; snapshot->native_mode = value; }
        value = call_getter(client, 0xb8, &ok);
        if (ok) { snapshot->emergency_heat_valid = true; snapshot->emergency_heat = value != 0; }
        value = call_getter(client, 0x5c, &ok);
        if (ok) { snapshot->eco_valid = true; snapshot->eco_mode = value; }
    }
    snapshot->valid = snapshot->current_temperature_valid || snapshot->target_temperature_valid || client != 0;
    pthread_mutex_unlock(&g_native_lock);
    return snapshot->valid;
}

bool native_command(NativeCommandKind kind, double value, std::string *error) {
    if (!g_hooks_active || !g_shared_bucket) {
        if (error) *error = "native write path is not available for this process/build";
        return false;
    }
    pthread_mutex_lock(&g_native_lock);
    volatile uint32_t *b = g_shared_bucket;
    bool result = false;
    switch (kind) {
        case NATIVE_SET_TARGET:
            b[0x43] = 1;
            b[0x44] = static_cast<uint32_t>(q16(value));
            result = apply_staged(error);
            break;
        case NATIVE_SET_TARGET_LOW:
            b[0x45] = 1;
            b[0x46] = static_cast<uint32_t>(q16(value));
            result = apply_staged(error);
            break;
        case NATIVE_SET_TARGET_HIGH:
            b[0x47] = 1;
            b[0x48] = static_cast<uint32_t>(q16(value));
            result = apply_staged(error);
            break;
        case NATIVE_SET_MODE:
            b[0x49] = 1;
            b[0x4a] = static_cast<uint32_t>(static_cast<int>(value));
            result = apply_staged(error);
            break;
        case NATIVE_SET_EMERGENCY_HEAT:
            b[0x4b] = 1;
            b[0x4c] = value != 0.0 ? 1 : 0;
            result = apply_staged(error);
            break;
        case NATIVE_SET_ECO: {
            uint32_t *client = client_object();
            uint32_t address = vtable_entry(client, 0x58);
            if (!address) {
                if (error) *error = "manual eco setter vtable entry unavailable";
                break;
            }
            typedef void (*Fn)(void *, int, int, const char *, int);
            reinterpret_cast<Fn>(address)(client, value != 0.0 ? 2 : 0, 2, "mqtt", value != 0.0 ? 1 : 4);
            result = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_native_lock);
    return result;
}
