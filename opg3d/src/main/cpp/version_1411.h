#pragma once

#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <string>

#include <sys/mman.h>
#include <unistd.h>

#include "cloud_guard.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Version-specific compatibility points for the supplied PG3D 14.1.1 ARMv7
// libil2cpp.so (SHA-256
// 5e4ab38ad6388ab187d0e7441ea6833838414fee5720d9dd483bf1e10eb33219).
// All ordinary features remain metadata-resolved. The only raw instruction
// change is the startup APK-signature decision, because that comparison lives
// inside a compiler-generated coroutine and has no safe managed boundary.
namespace version_1411 {
namespace detail {

using MethodInfo = void;
using ConnectSquadFn = bool (*)(bool siege_squad, const MethodInfo* method);
using AppendTimerStringFn = void (*)(void* static_context, void* text,
                                     const MethodInfo* method);
using MoveNextFn = bool (*)(void* self, const MethodInfo* method);
using InstanceVoidFn = void (*)(void* self, const MethodInfo* method);
using AsyncFloatFn = float (*)(void* self, const MethodInfo* method);
using AsyncBoolFn = bool (*)(void* self, const MethodInfo* method);

inline ConnectSquadFn g_connect_squad = nullptr;
inline AppendTimerStringFn g_append_timer_string = nullptr;
inline MoveNextFn g_switcher_start = nullptr;
inline InstanceVoidFn g_main_menu_awake = nullptr;
inline AsyncFloatFn g_async_progress = nullptr;
inline AsyncBoolFn g_async_done = nullptr;
inline AsyncBoolFn g_async_activate = nullptr;
inline const MethodInfo* g_mi_async_progress = nullptr;
inline const MethodInfo* g_mi_async_done = nullptr;
inline const MethodInfo* g_mi_async_activate = nullptr;
inline void* g_switcher_iterator_pc = nullptr;
inline void* g_switcher_iterator_self = nullptr;
inline void* g_switcher_progress = nullptr;
inline void* g_switcher_old_progress = nullptr;
inline void* g_switcher_next_scene = nullptr;
inline void* g_switcher_load_task = nullptr;
inline std::atomic<uint32_t> g_loader_steps{0u};
inline std::atomic<bool> g_main_menu_reached{false};
inline void* g_last_switcher_iterator = nullptr;
inline int32_t g_last_switcher_pc = INT32_MIN;
inline float g_last_switcher_progress = -1000.0f;
inline uint64_t g_last_switcher_log_ms = 0u;

// AppsMenu.<Start>c__Iterator1.MoveNext(), 14.1.1:
//   0xA9A980  cmp r0, #0
//   0xA9A984  beq 0xA9AAF0  ; signature accepted
// Re-signing takes the fall-through path, persists a tamper marker and queues
// ClosingScene. Only the condition code is changed (EQ -> AL); package-name,
// null/error and every unrelated startup check remain stock.
inline constexpr uintptr_t kSignatureDecisionRva = 0x00A9A984u;
inline constexpr uint32_t kExpectedCmp = 0xE3500000u;
inline constexpr uint32_t kExpectedBranch = 0x0A000059u;
inline constexpr uint32_t kAcceptedBranch = 0xEA000059u;

bool patch_word(uintptr_t address, uint32_t replacement) {
    const long page_size_raw = sysconf(_SC_PAGESIZE);
    if (page_size_raw <= 0) {
        LOGE("14.1.1: sysconf(_SC_PAGESIZE) failed");
        return false;
    }
    const uintptr_t page_size = static_cast<uintptr_t>(page_size_raw);
    const uintptr_t page = address & ~(page_size - 1u);
    if (mprotect(reinterpret_cast<void*>(page), page_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("14.1.1: mprotect RWX failed for signature decision: %s",
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
        LOGW("14.1.1: could not restore RX protection after signature patch: %s",
             std::strerror(errno));
    }
    return written;
}

void* replacement(ConnectSquadFn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void* trace_replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

void** original_slot(ConnectSquadFn* fn) {
    return reinterpret_cast<void**>(fn);
}

template <typename Fn>
void** trace_original_slot(Fn* fn) {
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

std::string read_string_field(void* object, void* field) {
    return il2cpp::to_utf8(read_field<void*>(object, field, nullptr), 160);
}

void read_async_state(void* operation, float* progress, int* done,
                      int* activate) {
    *progress = -1.0f;
    *done = -1;
    *activate = -1;
    if (operation == nullptr) return;
    if (g_async_progress != nullptr && g_mi_async_progress != nullptr) {
        *progress = g_async_progress(operation, g_mi_async_progress);
    }
    if (g_async_done != nullptr && g_mi_async_done != nullptr) {
        *done = g_async_done(operation, g_mi_async_done) ? 1 : 0;
    }
    if (g_async_activate != nullptr && g_mi_async_activate != nullptr) {
        *activate = g_async_activate(operation, g_mi_async_activate) ? 1 : 0;
    }
}

void hook_append_timer_string(void* static_context, void* text,
                              const MethodInfo* method) {
    if (!g_main_menu_reached.load(std::memory_order_acquire)) {
        const uint32_t step =
            g_loader_steps.fetch_add(1u, std::memory_order_relaxed) + 1u;
        const std::string value = il2cpp::to_utf8(text, 384);
        LOGI("startup-trace: loader-step#%" PRIu32 " '%s'",
             step, value.c_str());
    }
    if (g_append_timer_string != nullptr) {
        g_append_timer_string(static_context, text, method);
    }
}

bool hook_switcher_start(void* self, const MethodInfo* method) {
    const int32_t pc = read_field<int32_t>(
        self, g_switcher_iterator_pc, INT32_MIN);
    void* switcher = read_field<void*>(
        self, g_switcher_iterator_self, nullptr);
    const float progress = read_field<float>(
        switcher, g_switcher_progress, -1.0f);
    const float old_progress = read_field<float>(
        switcher, g_switcher_old_progress, -1.0f);
    void* operation = read_field<void*>(
        switcher, g_switcher_load_task, nullptr);
    const uint64_t now = opg3d_log::monotonic_ms();
    const float delta = progress > g_last_switcher_progress
                            ? progress - g_last_switcher_progress
                            : g_last_switcher_progress - progress;
    const bool emit = g_last_switcher_iterator != self ||
                      g_last_switcher_pc != pc || delta >= 0.005f ||
                      g_last_switcher_log_ms == 0u ||
                      now < g_last_switcher_log_ms ||
                      now - g_last_switcher_log_ms >= 2000u;
    if (emit) {
        g_last_switcher_iterator = self;
        g_last_switcher_pc = pc;
        g_last_switcher_progress = progress;
        g_last_switcher_log_ms = now;
        float operation_progress;
        int done;
        int activate;
        read_async_state(operation, &operation_progress, &done, &activate);
        LOGI("startup-trace: Switcher.Start ENTER pc=%d progress=%.3f "
             "old=%.3f next='%s' op=%p opProgress=%.3f done=%d activate=%d",
             pc, progress, old_progress,
             read_string_field(switcher, g_switcher_next_scene).c_str(),
             operation, operation_progress, done, activate);
    }

    if (g_switcher_start == nullptr) return false;
    const bool result = g_switcher_start(self, method);
    if (emit || !result) {
        switcher = read_field<void*>(self, g_switcher_iterator_self, nullptr);
        LOGI("startup-trace: Switcher.Start RETURN result=%d pc=%d "
             "progress=%.3f next='%s'",
             result ? 1 : 0,
             read_field<int32_t>(self, g_switcher_iterator_pc, INT32_MIN),
             read_field<float>(switcher, g_switcher_progress, -1.0f),
             read_string_field(switcher, g_switcher_next_scene).c_str());
    }
    return result;
}

void hook_main_menu_awake(void* self, const MethodInfo* method) {
    LOGI("startup-trace: MainMenuController.Awake ENTER self=%p", self);
    if (g_main_menu_awake != nullptr) g_main_menu_awake(self, method);
    g_main_menu_reached.store(true, std::memory_order_release);
    LOGI("startup-trace: MAIN MENU REACHED — MainMenuController.Awake returned");
}

bool resolve_trace_field(const char* klass, const char* name, void** out) {
    *out = il2cpp::find_field("", klass, name);
    if (*out != nullptr) return true;
    LOGE("startup-trace: cannot resolve field %s.%s", klass, name);
    return false;
}

bool resolve_async_getter(const char* name, void** out,
                          const MethodInfo** method_out) {
    void* method = il2cpp::find_method_info(
        "UnityEngine", "AsyncOperation", name, 0);
    *out = il2cpp::method_pointer(method);
    *method_out = method;
    if (*out != nullptr && method != nullptr) return true;
    LOGE("startup-trace: cannot resolve AsyncOperation.%s", name);
    return false;
}

bool install_startup_trace() {
    const char* iterator = "Switcher.<Start>c__Iterator0";
    bool resolved = true;
    resolved &= resolve_trace_field(iterator, "$PC", &g_switcher_iterator_pc);
    resolved &= resolve_trace_field(iterator, "$this", &g_switcher_iterator_self);
    resolved &= resolve_trace_field("Switcher", "_progress", &g_switcher_progress);
    resolved &= resolve_trace_field("Switcher", "oldProgress", &g_switcher_old_progress);
    resolved &= resolve_trace_field("Switcher", "nameNextScene", &g_switcher_next_scene);
    resolved &= resolve_trace_field("Switcher", "loadLevelTask", &g_switcher_load_task);
    resolved &= resolve_async_getter(
        "get_progress", reinterpret_cast<void**>(&g_async_progress),
        &g_mi_async_progress);
    resolved &= resolve_async_getter(
        "get_isDone", reinterpret_cast<void**>(&g_async_done),
        &g_mi_async_done);
    resolved &= resolve_async_getter(
        "get_allowSceneActivation", reinterpret_cast<void**>(&g_async_activate),
        &g_mi_async_activate);
    if (!resolved) {
        LOGE("startup-trace: metadata/getter resolution incomplete");
        return false;
    }

    int installed = 0;
    if (hook::install({"", "AppsMenu", "AppendTimerString", 1},
                      trace_replacement(&hook_append_timer_string),
                      trace_original_slot(&g_append_timer_string), false)) {
        ++installed;
    }
    if (hook::install({"", iterator, "MoveNext", 0},
                      trace_replacement(&hook_switcher_start),
                      trace_original_slot(&g_switcher_start), false)) {
        ++installed;
    }
    if (hook::install({"", "MainMenuController", "Awake", 0},
                      trace_replacement(&hook_main_menu_awake),
                      trace_original_slot(&g_main_menu_awake), false)) {
        ++installed;
    }
    LOGI("startup-trace: installed %d/3 read-only hooks "
         "(named milestones + Switcher heartbeat every 2s + menu boundary)",
         installed);
    return installed == 3;
}

bool hook_connect_squad(bool siege_squad, const MethodInfo* method) {
    cloud_guard::detail::force_cloud(
        "GameConnect.ConnectToPhotonSquad(bool)", true);
    return g_connect_squad != nullptr
               ? g_connect_squad(siege_squad, method)
               : false;
}

} // namespace detail

inline bool install_early_signature_patch(uintptr_t il2cpp_base) {
    if (il2cpp_base == 0u) {
        LOGE("14.1.1: no libil2cpp base for signature compatibility patch");
        return false;
    }

    const uintptr_t decision = il2cpp_base + detail::kSignatureDecisionRva;
    const uint32_t previous =
        *reinterpret_cast<const volatile uint32_t*>(decision - 4u);
    const uint32_t current =
        *reinterpret_cast<const volatile uint32_t*>(decision);

    if (previous != detail::kExpectedCmp) {
        LOGE("14.1.1: signature patch refused: preceding opcode at "
             "RVA 0x%08" PRIxPTR " is 0x%08" PRIx32 " (expected 0x%08" PRIx32 ")",
             detail::kSignatureDecisionRva - 4u, previous,
             detail::kExpectedCmp);
        return false;
    }
    if (current == detail::kAcceptedBranch) {
        LOGI("14.1.1: APK signature decision already accepts the current "
             "certificate");
        return true;
    }
    if (current != detail::kExpectedBranch) {
        LOGE("14.1.1: signature patch refused: opcode at RVA 0x%08" PRIxPTR
             " is 0x%08" PRIx32 " (expected 0x%08" PRIx32 ")",
             detail::kSignatureDecisionRva, current,
             detail::kExpectedBranch);
        return false;
    }

    if (!detail::patch_word(decision, detail::kAcceptedBranch)) {
        LOGE("14.1.1: APK signature acceptance branch was not written");
        return false;
    }

    LOGI("14.1.1: APK re-sign compatibility active at RVA 0x%08" PRIxPTR
         " (only signature mismatch -> success; package/error checks stock)",
         detail::kSignatureDecisionRva);
    return true;
}

inline bool install_runtime_hooks() {
    // 13.2.1 exposed ConnectToPhotonSquad() with no managed arguments.
    // 14.1.1 added the siegeSquad bool, so the old optional hook no longer
    // resolves. Cover the new overload explicitly and preserve its argument.
    const bool squad = hook::install(
        {"", "GameConnect", "ConnectToPhotonSquad", 1},
        detail::replacement(&detail::hook_connect_squad),
        detail::original_slot(&detail::g_connect_squad), true);
    if (!squad) {
        LOGE("14.1.1: GameConnect.ConnectToPhotonSquad(bool) hook failed");
        return false;
    }
    LOGI("14.1.1: squad Photon cloud-route hook armed (siege flag preserved)");

    // Diagnostics are deliberately non-blocking: failure to observe loading
    // must not disable the already working online/progression compatibility.
    if (!detail::install_startup_trace()) {
        LOGE("startup-trace: incomplete; gameplay remains enabled but the "
             "next 90%% stall may not be attributable");
    }
    return true;
}

} // namespace version_1411
