#pragma once

#include <android/log.h>

#include <atomic>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define LOG_TAG "OPG3D"

namespace opg3d_log {

// Every line gets a stable event number, monotonic time, Linux thread id and
// the return address of the function that emitted it. For inline-hook proxies
// the latter is the managed native caller in libil2cpp.so, so an unexpected
// PhotonNetwork.Disconnect can be mapped back to dump.cs without unwinding the
// stack. This deliberately avoids _Unwind_Backtrace: PG3D and this library use
// different ARM EHABI unwinders, and mixing their contexts has crashed before.
inline std::atomic<uint32_t> g_sequence{0u};
inline pthread_once_t g_clock_once = PTHREAD_ONCE_INIT;
inline uint64_t g_clock_origin_ms = 0u;

inline uint64_t monotonic_ms() {
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0u;
    return static_cast<uint64_t>(value.tv_sec) * 1000u +
           static_cast<uint64_t>(value.tv_nsec) / 1000000u;
}

inline void initialize_clock_origin() {
    g_clock_origin_ms = monotonic_ms();
}

inline const char* base_name(const char* path) {
    if (path == nullptr || *path == '\0') return "?";
    const char* slash = std::strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

inline void print(int priority, const void* return_address,
                  const char* format, ...)
    __attribute__((format(printf, 3, 4)));

inline void print(int priority, const void* return_address,
                  const char* format, ...) {
    char message[3072];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    pthread_once(&g_clock_once, initialize_clock_origin);
    const uint64_t now = monotonic_ms();
    const uint64_t elapsed = now >= g_clock_origin_ms
                                 ? now - g_clock_origin_ms
                                 : 0u;
    const uint32_t sequence =
        g_sequence.fetch_add(1u, std::memory_order_relaxed) + 1u;
    const long thread_id = static_cast<long>(syscall(SYS_gettid));

    // ARM Thumb return addresses can carry bit 0. dladdr() and RVA arithmetic
    // need the canonical even code address.
    const uintptr_t raw = reinterpret_cast<uintptr_t>(return_address);
    const uintptr_t pc = raw & ~static_cast<uintptr_t>(1u);
    Dl_info info{};
    const bool resolved = pc != 0u &&
                          dladdr(reinterpret_cast<const void*>(pc), &info) != 0 &&
                          info.dli_fbase != nullptr;

    char line[4096];
    if (resolved) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);
        const uintptr_t rva = pc >= base ? pc - base : 0u;
        std::snprintf(line, sizeof(line),
                      "[#%06" PRIu32 " +%06" PRIu64
                      "ms tid=%ld pc=%s+0x%" PRIxPTR "] %s",
                      sequence, elapsed, thread_id, base_name(info.dli_fname),
                      rva, message);
    } else {
        std::snprintf(line, sizeof(line),
                      "[#%06" PRIu32 " +%06" PRIu64
                      "ms tid=%ld pc=0x%" PRIxPTR "] %s",
                      sequence, elapsed, thread_id, pc, message);
    }
    __android_log_write(priority, LOG_TAG, line);
}

} // namespace opg3d_log

#if defined(__GNUC__) || defined(__clang__)
#define OPG3D_RETURN_ADDRESS() \
    __builtin_extract_return_addr(__builtin_return_address(0))
#else
#define OPG3D_RETURN_ADDRESS() nullptr
#endif

#define LOGI(...) ::opg3d_log::print(ANDROID_LOG_INFO,  OPG3D_RETURN_ADDRESS(), __VA_ARGS__)
#define LOGW(...) ::opg3d_log::print(ANDROID_LOG_WARN,  OPG3D_RETURN_ADDRESS(), __VA_ARGS__)
#define LOGE(...) ::opg3d_log::print(ANDROID_LOG_ERROR, OPG3D_RETURN_ADDRESS(), __VA_ARGS__)
