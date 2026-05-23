/*
 * Minimal read-only sys/mman.h shim for the Switch build. newlib on
 * devkitA64 has no mmap; the asset-pak loader (port_asset_pak_loader.cpp)
 * only ever maps pak files PROT_READ / MAP_PRIVATE, so emulate that with
 * malloc + read. Read-only mappings only — do not use for shared/writable.
 */
#ifndef TMC_COMPAT_SYS_MMAN_H
#define TMC_COMPAT_SYS_MMAN_H
#ifdef __SWITCH__

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_FAILED  ((void*)-1)

static inline void* mmap(void* addr, size_t length, int prot, int flags,
                         int fd, long offset) {
    (void)addr; (void)prot; (void)flags;
    if (length == 0) return MAP_FAILED;
    void* buf = malloc(length);
    if (!buf) return MAP_FAILED;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        free(buf);
        return MAP_FAILED;
    }
    size_t got = 0;
    while (got < length) {
        ssize_t r = read(fd, (char*)buf + got, length - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    return buf;
}

static inline int munmap(void* addr, size_t length) {
    (void)length;
    free(addr);
    return 0;
}

#endif /* __SWITCH__ */
#endif
