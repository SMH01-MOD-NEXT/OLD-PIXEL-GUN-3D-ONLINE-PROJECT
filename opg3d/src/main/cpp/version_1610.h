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

// Experimental compatibility layer for the supplied 16.1.x ARMv7 binary:
//   libil2cpp.so SHA-256
//   2aab620cb58a597e86975a78ab20987e71685b507456707ed42fa63fad54032b
//
// 16.1.x is partially obfuscated and enters AuthorizationScene before the
// lobby. The retired HTTP/WebSocket backend therefore blocks menu entry. This
// module deliberately does only two things:
//   1) accepts the current APK certificate at the verified AppsMenu branch;
//   2) enables and invokes the game's own offline transition after auth-scene
//      initialization, rather than fabricating authorization responses.
// Every 14.1.1 gameplay/Photon hook remains disabled until independently
// remapped against this binary.
namespace version_1610 {
namespace detail {

using MethodInfo = void;
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using StaticBoolFn = bool (*)(void* static_context, const MethodInfo* method);
using StaticStateSetterFn = void (*)(void* static_context, int32_t state,
                                     const MethodInfo* method);

inline InstanceVoidFn g_apps_menu_awake = nullptr;
inline InstanceVoidFn g_auth_awake = nullptr;
inline InstanceVoidFn g_auth_start = nullptr;
inline InstanceVoidFn g_main_menu_awake = nullptr;
inline InstanceVoidFn g_go_offline = nullptr;
inline const MethodInfo* g_mi_go_offline = nullptr;
inline StaticBoolFn g_offline_available = nullptr;
inline StaticStateSetterFn g_set_auth_state = nullptr;
inline void* g_auth_interface_field = nullptr;
inline void* g_last_auto_offline_controller = nullptr;
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

template <typename T>
T read_field(void* object, void* field, T fallback) {
    if (object == nullptr || field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return fallback;
    }
    alignas(8) uint8_t scratch[16] = {0};
    il2cpp::field_get_value(object, field, scratch);
    T value = fallback;
    std::memcpy(&value, scratch, sizeof(T));
    return value;
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

bool resolve_call(const hook::ManagedMethod& target, void** out_fn,
                  const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGE("16.1.x: cannot resolve %s.%s/%d", target.klass, target.method,
             target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

void hook_apps_menu_awake(void* self, const MethodInfo* method) {
    LOGI("16.1.x-trace: AppsMenu.Awake ENTER self=%p", self);
    if (g_apps_menu_awake != nullptr) g_apps_menu_awake(self, method);
    LOGI("16.1.x-trace: AppsMenu.Awake RETURN");
}

bool hook_offline_available(void* static_context, const MethodInfo* method) {
    (void)static_context;
    (void)method;
    LOGI("16.1.x-auth: stock offline route reported available");
    return true;
}

void hook_set_auth_state(void* static_context, int32_t state,
                         const MethodInfo* method) {
    LOGI("16.1.x-auth: state -> %d (%s)", state, state_name(state));
    if (g_set_auth_state != nullptr) {
        g_set_auth_state(static_context, state, method);
    }
}

void hook_auth_awake(void* self, const MethodInfo* method) {
    LOGI("16.1.x-auth: AuthSceneController.Awake ENTER self=%p", self);
    if (g_auth_awake != nullptr) g_auth_awake(self, method);
    LOGI("16.1.x-auth: AuthSceneController.Awake RETURN; offline callback "
         "should now be registered");
}

void hook_auth_start(void* self, const MethodInfo* method) {
    LOGI("16.1.x-auth: AuthSceneController.Start ENTER self=%p", self);
    if (g_auth_start != nullptr) g_auth_start(self, method);
    LOGI("16.1.x-auth: AuthSceneController.Start RETURN");

    if (self == nullptr || self == g_last_auto_offline_controller) return;
    void* auth_interface = read_field<void*>(self, g_auth_interface_field, nullptr);
    if (auth_interface == nullptr || g_go_offline == nullptr ||
        g_mi_go_offline == nullptr) {
        LOGE("16.1.x-auth: cannot invoke stock offline route: interface=%p "
             "method=%p info=%p", auth_interface,
             reinterpret_cast<void*>(g_go_offline), g_mi_go_offline);
        return;
    }

    g_last_auto_offline_controller = self;
    LOGW("16.1.x-auth: dead backend bypass — invoking stock "
         "AuthInterfaceController.OnGoOfflineClick after scene init");
    g_go_offline(auth_interface, g_mi_go_offline);
    LOGI("16.1.x-auth: stock offline callback returned");
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
    detail::g_auth_interface_field =
        il2cpp::find_field("", "AuthSceneController", "_authInterface");
    if (detail::g_auth_interface_field == nullptr) {
        LOGE("16.1.x-auth: AuthSceneController._authInterface not found");
        return false;
    }

    if (!detail::resolve_call(
            {"", "AuthInterfaceController", "OnGoOfflineClick", 0},
            reinterpret_cast<void**>(&detail::g_go_offline),
            &detail::g_mi_go_offline)) {
        return false;
    }

    const bool offline_available = hook::install(
        {"", "OfflineModController", u8"不丄且且上不丅丌专", 0},
        detail::replacement(&detail::hook_offline_available),
        detail::original_slot(&detail::g_offline_available), true);
    const bool auth_awake = hook::install(
        {"", "AuthSceneController", "Awake", 0},
        detail::replacement(&detail::hook_auth_awake),
        detail::original_slot(&detail::g_auth_awake), true);
    const bool auth_start = hook::install(
        {"", "AuthSceneController", "Start", 0},
        detail::replacement(&detail::hook_auth_start),
        detail::original_slot(&detail::g_auth_start), true);
    if (!offline_available || !auth_awake || !auth_start) {
        LOGE("16.1.x-auth: core stock-offline bypass hooks incomplete");
        return false;
    }

    if (!hook::install(
            {"", "AuthSceneController", u8"丄东丟丘一丕万丒丑", 1},
            detail::replacement(&detail::hook_set_auth_state),
            detail::original_slot(&detail::g_set_auth_state), false)) {
        LOGW("16.1.x-trace: auth state transitions unavailable");
    }
    if (!hook::install(
            {"", "AppsMenu", "Awake", 0},
            detail::replacement(&detail::hook_apps_menu_awake),
            detail::original_slot(&detail::g_apps_menu_awake), false)) {
        LOGW("16.1.x-trace: AppsMenu.Awake trace unavailable");
    }
    if (!hook::install(
            {"", "MainMenuController", "Awake", 0},
            detail::replacement(&detail::hook_main_menu_awake),
            detail::original_slot(&detail::g_main_menu_awake), false)) {
        LOGW("16.1.x-trace: MainMenuController.Awake trace unavailable");
    }

    LOGI("16.1.x-auth: experimental backend-first bypass armed via stock "
         "offline callback; no fabricated backend payloads");
    return true;
}

} // namespace version_1610
