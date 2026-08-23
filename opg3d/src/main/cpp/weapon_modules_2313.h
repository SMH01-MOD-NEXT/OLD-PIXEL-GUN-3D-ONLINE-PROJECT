#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "hook.h"
#include "log.h"

// 23.1.3 ARM64 weapon and armor module unlock.
//
// The ModulesController lists at +0x30/+0x38 are materialized definition
// lists. They already contain all 42 stock entries after OnInstanceCreated;
// appending the separate static catalog merely creates reference-identity
// duplicates (42 -> 84) and does not grant ownership.
//
// A module's stock current-level path first calls its inventory-count method
// and returns zero while that count is below one. Some armory paths query the
// count directly, which explains why the old level-only hook affected the
// modules already shown but did not make every weapon and armor module appear.
// Keep the game's catalogs and per-item equipped-module storage untouched:
// promote only a zero stock count to one and clamp the reported level to X.
namespace weapon_modules_2313 {

constexpr char kNamespace[] = "PGCompany";
constexpr char kModuleClass[] =
    "\xE4\xB8\x90\xE4\xB8\x89\xE4\xB8\x83\xE4\xB8\x96\xE4\xB8\x9D"
    "\xE4\xB8\x97\xE4\xB8\x8E\xE4\xB8\x9B\xE4\xB8\x8A";
constexpr char kCurrentLevel[] =
    "\xE4\xB8\x83\xE4\xB8\x94\xE4\xB8\x90\xE4\xB8\x9C\xE4\xB8\x92"
    "\xE4\xB8\x86\xE4\xB8\x91\xE4\xB8\x88\xE4\xB8\x87";
constexpr char kInventoryCount[] =
    "\xE4\xB8\x8E\xE4\xB8\x8F\xE4\xB8\x80\xE4\xB8\x97\xE4\xB8\x83"
    "\xE4\xB8\x9D\xE4\xB8\x80\xE4\xB8\x83\xE4\xB8\x8F";

constexpr int32_t kUnlockedLevel = 10;
constexpr uint64_t kLogBurst = 8u;
constexpr uint64_t kLogPeriod = 512u;

template <size_t LeftSize, size_t RightSize>
constexpr bool bytes_equal(const char (&left)[LeftSize],
                           const char (&right)[RightSize]) {
    if constexpr (LeftSize != RightSize) return false;
    for (size_t i = 0; i < LeftSize; ++i) {
        if (left[i] != right[i]) return false;
    }
    return true;
}

template <size_t Size>
constexpr bool contains_u5341(const char (&value)[Size]) {
    for (size_t i = 0; i + 2u < Size; ++i) {
        if (static_cast<unsigned char>(value[i]) == 0xE5u &&
            static_cast<unsigned char>(value[i + 1u]) == 0x8Du &&
            static_cast<unsigned char>(value[i + 2u]) == 0x81u) {
            return true;
        }
    }
    return false;
}

static_assert(bytes_equal(
                  kModuleClass,
                  "\xE4\xB8\x90\xE4\xB8\x89\xE4\xB8\x83"
                  "\xE4\xB8\x96\xE4\xB8\x9D\xE4\xB8\x97"
                  "\xE4\xB8\x8E\xE4\xB8\x9B\xE4\xB8\x8A"),
              "23.1.3 module class UTF-8 drift");
static_assert(bytes_equal(
                  kCurrentLevel,
                  "\xE4\xB8\x83\xE4\xB8\x94\xE4\xB8\x90"
                  "\xE4\xB8\x9C\xE4\xB8\x92\xE4\xB8\x86"
                  "\xE4\xB8\x91\xE4\xB8\x88\xE4\xB8\x87"),
              "23.1.3 current-level method UTF-8 drift");
static_assert(bytes_equal(
                  kInventoryCount,
                  "\xE4\xB8\x8E\xE4\xB8\x8F\xE4\xB8\x80"
                  "\xE4\xB8\x97\xE4\xB8\x83\xE4\xB8\x9D"
                  "\xE4\xB8\x80\xE4\xB8\x83\xE4\xB8\x8F"),
              "23.1.3 inventory-count method UTF-8 drift");

// U+5341 (十) is not part of the verified obfuscated identifier alphabet.
static_assert(!contains_u5341(kModuleClass) &&
                  !contains_u5341(kCurrentLevel) &&
                  !contains_u5341(kInventoryCount),
              "unverified CJK identifier transcription");

using InstanceIntFn = int32_t (*)(void*, void*);

inline InstanceIntFn g_orig_inventory_count = nullptr;
inline InstanceIntFn g_orig_current_level = nullptr;
inline std::atomic<uint64_t> g_count_promotions{0u};
inline std::atomic<uint64_t> g_level_calls{0u};

inline bool should_log(uint64_t call) {
    return call <= kLogBurst || call % kLogPeriod == 0u;
}

inline int32_t inventory_count_hook(void* self, void* method) {
    const int32_t original =
        g_orig_inventory_count != nullptr
            ? g_orig_inventory_count(self, method)
            : 0;

    // Preserve positive stock quantities and negative initialization/error
    // sentinels. Only zero means a valid module definition is not owned.
    if (self == nullptr || original != 0) return original;

    const uint64_t promotion =
        g_count_promotions.fetch_add(1u, std::memory_order_relaxed) + 1u;
    if (should_log(promotion)) {
        LOGI("23.1.3-modules: inventory count 0 -> 1 (promotion=%llu)",
             static_cast<unsigned long long>(promotion));
    }
    return 1;
}

inline int32_t current_level_hook(void* self, void* method) {
    const int32_t original =
        g_orig_current_level != nullptr
            ? g_orig_current_level(self, method)
            : 0;
    const int32_t effective =
        self != nullptr && original < kUnlockedLevel
            ? kUnlockedLevel
            : original;

    const uint64_t call =
        g_level_calls.fetch_add(1u, std::memory_order_relaxed) + 1u;
    if (should_log(call)) {
        LOGI("23.1.3-modules: level %d -> %d (call=%llu)",
             original,
             effective,
             static_cast<unsigned long long>(call));
    }
    return effective;
}

inline bool install_hooks() {
    const bool count_installed = hook::install(
        {kNamespace, kModuleClass, kInventoryCount, 0},
        reinterpret_cast<void*>(inventory_count_hook),
        reinterpret_cast<void**>(&g_orig_inventory_count),
        true);
    if (!count_installed) return false;

    const bool level_installed = hook::install(
        {kNamespace, kModuleClass, kCurrentLevel, 0},
        reinterpret_cast<void*>(current_level_hook),
        reinterpret_cast<void**>(&g_orig_current_level),
        true);
    if (!level_installed) return false;

    LOGI("23.1.3-modules: armed: weapon and armor definitions remain in "
         "the stock catalog; zero inventory counts become one and levels "
         "are clamped to X");
    return true;
}

} // namespace weapon_modules_2313
