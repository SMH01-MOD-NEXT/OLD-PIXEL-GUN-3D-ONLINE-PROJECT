#pragma once

#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Version guard and passive diagnostics for the supplied 16.1.x ARMv7 binary:
//   libil2cpp.so SHA-256
//   2aab620cb58a597e86975a78ab20987e71685b507456707ed42fa63fad54032b
//
// Authentication behavior lives in backend_local_1610.h. This module only
// accepts the current APK certificate at the verified AppsMenu branch and
// records lifecycle/auth-state diagnostics. It does not choose an offline or
// online route and does not fabricate backend responses.
namespace version_1610 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using StaticStateSetterFn = void (*)(void* static_context, int32_t state,
                                     const MethodInfo* method);

inline InstanceVoidFn g_apps_menu_awake = nullptr;
inline InstanceVoidFn g_auth_awake = nullptr;
inline InstanceVoidFn g_main_menu_awake = nullptr;
inline StaticStateSetterFn g_set_auth_state = nullptr;
inline std::atomic<bool> g_main_menu_reached{false};

// AppsMenu.<Start>.MoveNext(), supplied 16.1.x binary:
//   0x02AAC900  cmp r0, #0
//   0x02AAC904  beq 0x02AACA50  ; package signature accepted
// The mismatch path logs the current signature, stores the tamper condition
// and diverts startup. As in 14.1.1, only EQ -> AL is changed; package-name,
// null/error and every unrelated startup check remain stock.
inline constexpr uintptr_t kSignatureDecisionRva = 0x02AAC904u;
inline constexpr uint32_t kExpectedCmp = 0xE3500000u;
inline constexpr uint32_t kExpectedBranch = 0x0A000051u;
inline constexpr uint32_t kAcceptedBranch = 0xEA000051u;

bool patch_word(uintptr_t address, uint32_t replacement) {
    const long page_size_raw = sysconf(_SC_PAGESIZE);
    if (page_size_raw <= 0) {
        LOGE("16.1.x: sysconf(_SC_PAGESIZE) failed");
        return false;
    }
    const uintptr_t page_size = static_cast<uintptr_t>(page_size_raw);
    const uintptr_t page = address & ~(page_size - 1u);
    if (mprotect(reinterpret_cast<void*>(page), page_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("16.1.x: mprotect RWX failed for signature decision: %s",
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
        LOGW("16.1.x: could not restore RX after signature patch: %s",
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
        default: return "Unknown";
    }
}

void hook_apps_menu_awake(void* self, const MethodInfo* method) {
    LOGI("16.1.x-trace: AppsMenu.Awake ENTER self=%p", self);
    if (g_apps_menu_awake != nullptr) g_apps_menu_awake(self, method);
    LOGI("16.1.x-trace: AppsMenu.Awake RETURN");
}

void hook_set_auth_state(void* static_context, int32_t state,
                         const MethodInfo* method) {
    LOGI("16.1.x-auth: direct state setter -> %d (%s)", state,
         state_name(state));
    if (g_set_auth_state != nullptr) {
        g_set_auth_state(static_context, state, method);
    }
}

void hook_auth_awake(void* self, const MethodInfo* method) {
    LOGI("16.1.x-auth: AuthSceneController.Awake ENTER self=%p", self);
    if (g_auth_awake != nullptr) g_auth_awake(self, method);
    LOGI("16.1.x-auth: AuthSceneController.Awake RETURN; local serializers "
         "and auth listeners initialized by stock code");
}

void hook_main_menu_awake(void* self, const MethodInfo* method) {
    LOGI("16.1.x-trace: MainMenuController.Awake ENTER self=%p", self);
    if (g_main_menu_awake != nullptr) g_main_menu_awake(self, method);
    g_main_menu_reached.store(true, std::memory_order_release);
    LOGI("16.1.x-trace: MAIN MENU REACHED — MainMenuController.Awake returned");
}

} // namespace detail

inline bool install_early_signature_patch(uintptr_t il2cpp_base) {
    if (il2cpp_base == 0u) {
        LOGE("16.1.x: no libil2cpp base for signature compatibility patch");
        return false;
    }

    const uintptr_t decision = il2cpp_base + detail::kSignatureDecisionRva;
    const uint32_t previous =
        *reinterpret_cast<const volatile uint32_t*>(decision - 4u);
    const uint32_t current =
        *reinterpret_cast<const volatile uint32_t*>(decision);
    if (previous != detail::kExpectedCmp) {
        LOGE("16.1.x: signature patch refused: preceding opcode at RVA "
             "0x%08" PRIxPTR " is 0x%08" PRIx32,
             detail::kSignatureDecisionRva - 4u, previous);
        return false;
    }
    if (current == detail::kAcceptedBranch) {
        LOGI("16.1.x: APK signature decision already patched");
        return true;
    }
    if (current != detail::kExpectedBranch) {
        LOGE("16.1.x: signature patch refused: opcode at RVA 0x%08" PRIxPTR
             " is 0x%08" PRIx32 " (expected 0x%08" PRIx32 ")",
             detail::kSignatureDecisionRva, current,
             detail::kExpectedBranch);
        return false;
    }
    if (!detail::patch_word(decision, detail::kAcceptedBranch)) {
        LOGE("16.1.x: signature acceptance branch was not written");
        return false;
    }
    LOGI("16.1.x: APK re-sign compatibility active at RVA 0x%08" PRIxPTR,
         detail::kSignatureDecisionRva);
    return true;
}

inline bool install_runtime_hooks() {
    const bool auth_awake = hook::install(
        {"", "AuthSceneController", "Awake", 0},
        detail::replacement(&detail::hook_auth_awake),
        detail::original_slot(&detail::g_auth_awake), true);
    if (!auth_awake) {
        LOGE("16.1.x-trace: AuthSceneController.Awake trace unavailable");
        return false;
    }

    int optional = 0;
    if (hook::install(
            {"", "AuthSceneController", u8"丄东丟丘一丕万丒丑", 1},
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
            {"", "MainMenuController", "Awake", 0},
            detail::replacement(&detail::hook_main_menu_awake),
            detail::original_slot(&detail::g_main_menu_awake), false)) {
        ++optional;
    }

    LOGI("16.1.x-trace: version diagnostics armed (auth-awake=OK, optional=%d/3)",
         optional);
    return true;
}

} // namespace version_1610
