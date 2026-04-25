#pragma once
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

// Recover the enclosing struct from a pointer to a member.
#define container_of(ptr, type, member) \
    (reinterpret_cast<type*>(                      \
        reinterpret_cast<char*>(                   \
            const_cast<std::remove_cv_t<           \
                std::remove_pointer_t<             \
                    decltype(ptr)>>*>(ptr))         \
        - offsetof(type, member)))

// FNV-1a hash — fast, good distribution for short strings.
inline uint64_t str_hash(const uint8_t* data, size_t len) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < len; ++i) {
        h = (h ^ data[i]) * 0x01000193u;
    }
    return h;
}

inline void log_msg(const char* msg) {
    std::fprintf(stderr, "[INFO] %s\n", msg);
}

inline void log_errno(const char* msg) {
    std::fprintf(stderr, "[ERROR errno=%d] %s\n", errno, msg);
}

[[noreturn]] inline void die(const char* msg) {
    std::fprintf(stderr, "[FATAL errno=%d] %s\n", errno, msg);
    std::abort();
}
