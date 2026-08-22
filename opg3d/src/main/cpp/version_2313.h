#pragma once

#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

#include "hook.h"
#include "log.h"

// Exact-build startup compatibility and passive lifecycle diagnostics for the
// supplied PG3D 23.1.3 ARM64 libil2cpp.so. Backend behavior is isolated in
// backend_local_2313.h.
namespace version_2313 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using InstanceIteratorFn = void* (*)(void* self, const MethodInfo* method);
using StaticStateSetterFn = void (*)(int32_t state,
                                     const MethodInfo* method);

inline InstanceVoidFn g_apps_menu_awake = nullptr;
inline InstanceIteratorFn g_apps_menu_start = nullptr;
inline InstanceVoidFn g_auth_awake = nullptr;
inline InstanceVoidFn g_training_awake = nullptr;
inline InstanceVoidFn g_main_menu_awake = nullptr;
inline StaticStateSetterFn g_set_auth_state = nullptr;
inline std::atomic<bool> g_training_reached{false};
inline std::atomic<bool> g_main_menu_reached{false};

// AppsMenu.<Start>d__.MoveNext(), supplied 23.1.3 AArch64 binary:
//   0x04372974  bl  String.Compare
//   0x04372978  cbz w0, 0x04372B04  ; package signature accepted
// The mismatch path logs and diverts startup. Replace only that verified CBZ
// with an unconditional B to the same accepted target. This is an A64 encoding
// derived from the supplied ELF; no 16.1.0 ARM32 opcode is reused.
inline constexpr uintptr_t kSignatureDecisionRva = 0x04372978u;
inline constexpr uintptr_t kSignatureAcceptedTargetRva = 0x04372B04u;
inline constexpr uint32_t kExpectedCompareCall = 0x9404EA68u;
inline constexpr uint32_t kExpectedDecision = 0x34000C60u;
inline constexpr uint32_t kAcceptedDecision = 0x14000063u;

bool patch_word(uintptr_t address, uint32_t replacement) {
    const long page_size_raw = sysconf(_SC_PAGESIZE);
    if (page_size_raw <= 0) {
        LOGE("23.1.3: sysconf(_SC_PAGESIZE) failed");
        return false;
    }
    const uintptr_t page_size = static_cast<uintptr_t>(page_size_raw);
    const uintptr_t page = address & ~(page_size - 1u);
    if (mprotect(reinterpret_cast<void*>(page), page_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("23.1.3: mprotect RWX failed for signature decision: %s",
             std::strerror(errno));
        return false;
    }

    auto* word = reinterpret_cast<volatile uint32_t*>(address);
    *word = replacement;
    __builtin___clear_cache(reinterpret_cast<char*>(address),
                            reinterpret_cast<char*>(address + sizeof(uint32_t)));
    const bool written = (*word == replacement);

    if (mprotect(reinterpret_cast<void*>(page), page_size,
                 PROT_READ | PROT_EXEC) != 0) {
        LOGW("23.1.3: could not restore RX after signature patch: %s",
             std::strerror(errno));
    }
    return written;
}

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

const char* state_name(int32_t state) {
    switch (state) {
        case -1: return "None";
        case 0: return "Initial";
        case 1: return "Authorizing";
        case 2: return "Authorized";
        case 3: return "FullySynchronized";
        case 4: return "Empty";
        case 5: return "CheckBindedId";
        case 6: return "ChooseId";
        case 7: return "ChooseProgress";
        case 8: return "SendCachedCommands";
        case 9: return "SynchronizeProgress";
        case 10: return "CheckAppVersion";
        case 11: return "Easy";
        case 12: return "CheckConnection";
        case 13: return "SendProgress";
        case 14: return "WaitAsync";
        case 15: return "TechnicalWorks";
        case 16: return "Login";
        default: return "Unknown";
    }
}

void hook_apps_menu_awake(void* self, const MethodInfo* method) {
    LOGI("23.1.3-trace: AppsMenu.Awake ENTER self=%p", self);
    if (g_apps_menu_awake != nullptr) g_apps_menu_awake(self, method);
    LOGI("23.1.3-trace: AppsMenu.Awake RETURN");
}

void* hook_apps_menu_start(void* self, const MethodInfo* method) {
    LOGI("23.1.3-trace: AppsMenu.Start iterator requested self=%p", self);
    return g_apps_menu_start != nullptr ? g_apps_menu_start(self, method)
                                        : nullptr;
}

void hook_set_auth_state(int32_t state, const MethodInfo* method) {
    LOGI("23.1.3-auth: direct state setter -> %d (%s)", state,
         state_name(state));
    if (g_set_auth_state != nullptr) {
        g_set_auth_state(state, method);
    }
}

void hook_auth_awake(void* self, const MethodInfo* method) {
    LOGI("23.1.3-auth: AuthSceneController.Awake ENTER self=%p", self);
    if (g_auth_awake != nullptr) g_auth_awake(self, method);
    LOGI("23.1.3-auth: AuthSceneController.Awake RETURN; stock local model "
         "owners initialized");
}

void hook_training_awake(void* self, const MethodInfo* method) {
    LOGI("23.1.3-trace: TrainingController.Awake ENTER self=%p", self);
    if (g_training_awake != nullptr) g_training_awake(self, method);
    g_training_reached.store(true, std::memory_order_release);
    LOGI("23.1.3-trace: TUTORIAL REACHED");
}

void hook_main_menu_awake(void* self, const MethodInfo* method) {
    LOGI("23.1.3-trace: MainMenuController.Awake ENTER self=%p", self);
    if (g_main_menu_awake != nullptr) g_main_menu_awake(self, method);
    g_main_menu_reached.store(true, std::memory_order_release);
    LOGI("23.1.3-trace: MAIN MENU REACHED");
}

} // namespace detail

inline bool install_early_signature_patch(uintptr_t il2cpp_base) {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif
    if (il2cpp_base == 0u) {
        LOGE("23.1.3: no libil2cpp base for signature compatibility patch");
        return false;
    }

    const uintptr_t decision = il2cpp_base + detail::kSignatureDecisionRva;
    const uint32_t previous =
        *reinterpret_cast<const volatile uint32_t*>(decision - 4u);
    const uint32_t current =
        *reinterpret_cast<const volatile uint32_t*>(decision);
    if (previous != detail::kExpectedCompareCall) {
        LOGE("23.1.3: signature patch refused: preceding A64 opcode at RVA "
             "0x%08" PRIxPTR " is 0x%08" PRIx32,
             detail::kSignatureDecisionRva - 4u, previous);
        return false;
    }
    if (current == detail::kAcceptedDecision) {
        LOGI("23.1.3: APK signature decision already patched");
        return true;
    }
    if (current != detail::kExpectedDecision) {
        LOGE("23.1.3: signature patch refused: A64 opcode at RVA 0x%08" PRIxPTR
             " is 0x%08" PRIx32 " (expected 0x%08" PRIx32 ")",
             detail::kSignatureDecisionRva, current,
             detail::kExpectedDecision);
        return false;
    }
    if (!detail::patch_word(decision, detail::kAcceptedDecision)) {
        LOGE("23.1.3: signature acceptance branch was not written");
        return false;
    }
    LOGI("23.1.3: APK re-sign compatibility active (A64 RVA 0x%08" PRIxPTR
         " -> 0x%08" PRIxPTR ")", detail::kSignatureDecisionRva,
         detail::kSignatureAcceptedTargetRva);
    return true;
}

inline bool install_runtime_hooks() {
    const bool auth_awake = hook::install(
        {"", "AuthSceneController", "Awake", 0},
        detail::replacement(&detail::hook_auth_awake),
        detail::original_slot(&detail::g_auth_awake), true);
    if (!auth_awake) {
        LOGE("23.1.3-trace: AuthSceneController.Awake trace unavailable");
        return false;
    }

    int optional = 0;
    if (hook::install(
            {"", "AuthSceneController", u8"丂丙丝丒世万专丑业", 1},
            detail::replacement(&detail::hook_set_auth_state),
            detail::original_slot(&detail::g_set_auth_state), false)) {
        ++optional;
    }
    if (hook::install(
            {"", "AppsMenu", "Awake", 0},
            detail::replacement(&detail::hook_apps_menu_awake),
            detail::original_slot(&detail::g_apps_menu_awake), false)) {
        ++optional;
    }
    if (hook::install(
            {"", "AppsMenu", "Start", 0},
            detail::replacement(&detail::hook_apps_menu_start),
            detail::original_slot(&detail::g_apps_menu_start), false)) {
        ++optional;
    }
    if (hook::install(
            {"", "TrainingController", "Awake", 0},
            detail::replacement(&detail::hook_training_awake),
            detail::original_slot(&detail::g_training_awake), false)) {
        ++optional;
    }
    if (hook::install(
            {"", "MainMenuController", "Awake", 0},
            detail::replacement(&detail::hook_main_menu_awake),
            detail::original_slot(&detail::g_main_menu_awake), false)) {
        ++optional;
    }

    LOGI("23.1.3-trace: lifecycle diagnostics armed "
         "(auth-awake=OK optional=%d/5)", optional);
    return true;
}

} // namespace version_2313
