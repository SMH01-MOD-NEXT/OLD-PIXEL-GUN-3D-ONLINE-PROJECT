#pragma once

#include <cinttypes>
#include <cstdint>

#include <unistd.h>

#include "log.h"

// Startup synchronization for the exact supplied 23.1.3 ARM64 libil2cpp.so.
//
// In this Unity/IL2CPP build il2cpp_domain_get() is NOT a safe readiness poll.
// Before il2cpp_init publishes the root domain, the exported function enters a
// lazy-initialization path that dereferences an uninitialized runtime service.
// On device that produced the deterministic chain
//   0x12DE5FC -> 0x12B463C -> 0x1299108 (x0 == 0, fault +0x132).
//
// The function's exact A64 implementation first loads a root-domain singleton
// slot and returns it when non-null. Validate the instructions and poll that
// slot without invoking managed runtime code. No timing guess and no unsafe
// call into the lazy path are required.
namespace il2cpp_runtime_2313 {
namespace detail {

inline constexpr uintptr_t kDomainGetExportRva = 0x0124E47Cu;
inline constexpr uintptr_t kDomainSingletonAdrpRva = 0x012DE5CCu;
inline constexpr uintptr_t kDomainSingletonLoadRva = 0x012DE5D0u;
inline constexpr uintptr_t kDomainSingletonSlotRva = 0x06C74618u;

inline constexpr uint32_t kExpectedExportBranch = 0x14024052u;
inline constexpr uint32_t kExpectedSingletonAdrp = 0xD002CCB4u;
inline constexpr uint32_t kExpectedSingletonLoad = 0xF9430E80u;

inline uint32_t read_word(uintptr_t address) {
    return *reinterpret_cast<const volatile uint32_t*>(address);
}

} // namespace detail

inline void* wait_for_domain(uintptr_t il2cpp_base, int attempts,
                             useconds_t delay_us) {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif
    if (il2cpp_base == 0u || attempts <= 0) {
        LOGE("23.1.3-runtime: invalid domain readiness arguments");
        return nullptr;
    }

    const uint32_t export_branch = detail::read_word(
        il2cpp_base + detail::kDomainGetExportRva);
    const uint32_t adrp = detail::read_word(
        il2cpp_base + detail::kDomainSingletonAdrpRva);
    const uint32_t load = detail::read_word(
        il2cpp_base + detail::kDomainSingletonLoadRva);
    if (export_branch != detail::kExpectedExportBranch ||
        adrp != detail::kExpectedSingletonAdrp ||
        load != detail::kExpectedSingletonLoad) {
        LOGE("23.1.3-runtime: root-domain wait refused; unexpected A64 "
             "fingerprint export=%08" PRIx32 " adrp=%08" PRIx32
             " load=%08" PRIx32,
             export_branch, adrp, load);
        return nullptr;
    }

    auto* slot = reinterpret_cast<const uintptr_t*>(
        il2cpp_base + detail::kDomainSingletonSlotRva);
    for (int i = 0; i < attempts; ++i) {
        const uintptr_t published =
            __atomic_load_n(slot, __ATOMIC_ACQUIRE);
        if (published != 0u) {
            LOGI("23.1.3-runtime: IL2CPP root domain published after %" PRIu64
                 " ms (safe slot RVA 0x%08" PRIxPTR ")",
                 static_cast<uint64_t>(i) * delay_us / 1000u,
                 detail::kDomainSingletonSlotRva);
            return reinterpret_cast<void*>(published);
        }
        usleep(delay_us);
    }

    LOGE("23.1.3-runtime: IL2CPP root domain was not published after %" PRIu64
         " ms; refusing unsafe il2cpp_domain_get() call",
         static_cast<uint64_t>(attempts) * delay_us / 1000u);
    return nullptr;
}

} // namespace il2cpp_runtime_2313
