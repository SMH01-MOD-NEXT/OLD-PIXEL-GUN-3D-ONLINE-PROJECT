#include "hook.h"

#include <cinttypes>
#include <cstdint>
#include <mutex>

#include <shadowhook.h>

#include "il2cpp.h"
#include "log.h"

namespace hook {
namespace {

// shadowhook is initialized once per process. UNIQUE mode is a deliberate
// choice: each address is hooked exactly once, and in this mode orig_addr is
// a directly callable trampoline to the original (the same contract DobbyHook
// used to have). That's why the proxy functions in photon_hooks.cpp call the
// original directly, without the SHADOWHOOK_CALL_PREV / SHADOWHOOK_STACK_SCOPE
// wrappers that SHARED mode would require.
std::once_flag g_engine_once;
bool g_engine_ready = false;

void ensure_engine() {
    std::call_once(g_engine_once, [] {
        const int rc = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
        g_engine_ready = (rc == SHADOWHOOK_ERRNO_OK);
        if (g_engine_ready) {
            LOGI("hook: %s ready (UNIQUE mode)", shadowhook_get_version());
        } else {
            LOGE("hook: shadowhook_init failed errno=%d (%s)",
                 rc, shadowhook_to_errmsg(rc));
        }
    });
}

} // namespace

const char* engine_version() {
    return shadowhook_get_version();
}

bool install(const ManagedMethod& target, void* replacement, void** original,
             bool required) {
    if (replacement == nullptr || original == nullptr) {
        LOGE("hook: invalid arguments for %s.%s", target.klass, target.method);
        return false;
    }

    ensure_engine();
    if (!g_engine_ready) {
        LOGE("hook: engine unavailable; cannot hook %s.%s/%d",
             target.klass, target.method, target.args_count);
        return false;
    }

    void* method_info = il2cpp::find_method_info(
        target.namespaze, target.klass, target.method, target.args_count);
    if (method_info == nullptr) {
        if (required) {
            LOGE("hook: REQUIRED method not found: %s.%s/%d",
                 target.klass, target.method, target.args_count);
        } else {
            LOGW("hook: optional method not found: %s.%s/%d",
                 target.klass, target.method, target.args_count);
        }
        return false;
    }

    void* address = il2cpp::method_pointer(method_info);
    if (address == nullptr) {
        LOGE("hook: null methodPointer: %s.%s/%d",
             target.klass, target.method, target.args_count);
        return false;
    }

    *original = nullptr;
    void* stub = shadowhook_hook_func_addr(address, replacement, original);
    if (stub == nullptr) {
        const int err = shadowhook_get_errno();
        LOGE("hook: shadowhook_hook_func_addr failed errno=%d (%s): %s.%s/%d @ %p",
             err, shadowhook_to_errmsg(err),
             target.klass, target.method, target.args_count, address);
        return false;
    }

    // Fail-closed: the proxies in photon_hooks.cpp call the original
    // unconditionally, so an installed hook with a null orig_addr is a
    // guaranteed SIGSEGV on the first intercepted call (for
    // Switcher.SetUpPhoton that's right at game start). Rolling back and
    // losing one hook is better than crashing the process.
    if (*original == nullptr) {
        LOGE("hook: engine gave no trampoline for %s.%s/%d @ %p; rolling back",
             target.klass, target.method, target.args_count, address);
        shadowhook_unhook(stub);
        return false;
    }

    LOGI("hook: installed %s.%s/%d @ %p",
         target.klass, target.method, target.args_count, address);
    return true;
}

} // namespace hook
