#include "arm_hook.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

static void set_error(char *error, size_t size, const char *message) {
    if (!error || size == 0) return;
    snprintf(error, size, "%s", message);
}

#if defined(__arm__)
static bool make_writable(void *address, size_t size, char *error, size_t error_size) {
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;
    uintptr_t begin = reinterpret_cast<uintptr_t>(address) & ~(static_cast<uintptr_t>(page_size) - 1);
    uintptr_t end = (reinterpret_cast<uintptr_t>(address) + size + page_size - 1) & ~(static_cast<uintptr_t>(page_size) - 1);
    if (mprotect(reinterpret_cast<void *>(begin), end - begin,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        if (error && error_size) snprintf(error, error_size, "mprotect: %s", strerror(errno));
        return false;
    }
    return true;
}

static void write_absolute_jump(uint32_t *where, void *destination) {
    // ARM mode: ldr pc, [pc, #-4], followed by the absolute destination.
    where[0] = 0xE51FF004u;
    where[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(destination));
}
#endif

}  // namespace

bool arm_install_hook(void *target, void *replacement, void **trampoline,
                      char *error, size_t error_size) {
#if !defined(__arm__)
    (void)target;
    (void)replacement;
    (void)trampoline;
    set_error(error, error_size, "inline hooks are ARM-only");
    return false;
#else
    if (!target || !replacement || !trampoline) {
        set_error(error, error_size, "invalid hook argument");
        return false;
    }
    uintptr_t target_address = reinterpret_cast<uintptr_t>(target);
    if (target_address & 1u) {
        set_error(error, error_size, "Thumb targets are not supported");
        return false;
    }

    void *stub = mmap(0, 16, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stub == MAP_FAILED) {
        if (error && error_size) snprintf(error, error_size, "mmap: %s", strerror(errno));
        return false;
    }
    memcpy(stub, target, 8);
    write_absolute_jump(reinterpret_cast<uint32_t *>(static_cast<unsigned char *>(stub) + 8),
                        reinterpret_cast<void *>(target_address + 8));
    __builtin___clear_cache(static_cast<char *>(stub), static_cast<char *>(stub) + 16);

    if (!make_writable(target, 8, error, error_size)) {
        munmap(stub, 16);
        return false;
    }
    write_absolute_jump(reinterpret_cast<uint32_t *>(target), replacement);
    __builtin___clear_cache(static_cast<char *>(target), static_cast<char *>(target) + 8);
    *trampoline = stub;
    return true;
#endif
}
