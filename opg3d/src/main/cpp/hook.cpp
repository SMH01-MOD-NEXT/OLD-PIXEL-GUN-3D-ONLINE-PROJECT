#include "hook.h"

#include <cinttypes>
#include <cstdint>

#include <dobby.h>

#include "il2cpp.h"
#include "log.h"

namespace hook {

bool install(const ManagedMethod& target, void* replacement, void** original,
             bool required) {
    if (replacement == nullptr || original == nullptr) {
        LOGE("hook: invalid arguments for %s.%s", target.klass, target.method);
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
    const int rc = DobbyHook(address, replacement, original);
    if (rc != 0 || *original == nullptr) {
        LOGE("hook: DobbyHook failed rc=%d: %s.%s/%d @ %p",
             rc, target.klass, target.method, target.args_count, address);
        return false;
    }

    LOGI("hook: installed %s.%s/%d @ %p",
         target.klass, target.method, target.args_count, address);
    return true;
}

} // namespace hook
