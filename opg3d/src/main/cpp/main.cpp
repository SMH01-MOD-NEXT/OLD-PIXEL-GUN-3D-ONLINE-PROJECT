#include <cinttypes>
#include <cstddef>
#include <cstdint>

#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include "backend_local_2313.h"
#include "config.h"
#include "elf_sym.h"
#include "il2cpp.h"
#include "il2cpp_runtime_2313.h"
#include "log.h"
#include "obb_provisioner.h"
#include "photon_2313.h"
#include "photon_default_plugin_2313.h"
#include "photon_trace_2313.h"
#include "startup_guards_2313.h"
#include "startup_trace_2313.h"
#include "version_2313.h"

namespace {

constexpr const char* kIl2Cpp = "libil2cpp.so";
constexpr int kWaitSteps = 6000;
constexpr useconds_t kWaitStepUs = 10 * 1000;
constexpr int kStableChecks = 25;
constexpr useconds_t kSettleUs = 100 * 1000;

size_t assembly_count(void* domain) {
    if (domain == nullptr || il2cpp::domain_get_assemblies == nullptr) return 0u;
    size_t count = 0u;
    il2cpp::domain_get_assemblies(domain, &count);
    if (count > 8192u) return 0u;
    return count;
}

void* init_thread(void*) {
    LOGI("init: libopg3d build %s", OPG3D_BUILD_STAMP);
    LOGI("init: [0/6] 23.1.3 ARM64 local-backend + Photon Cloud bootstrap started");

    uintptr_t base = 0u;
    bool found = false;
    for (int i = 0; i < kWaitSteps && !found; ++i) {
        found = elfsym::find_library(kIl2Cpp, &base);
        if (!found) usleep(kWaitStepUs);
    }
    if (!found) {
        LOGE("init: %s not found after 60 seconds", kIl2Cpp);
        return nullptr;
    }
    LOGI("init: [1/6] %s found, base=0x%" PRIxPTR, kIl2Cpp, base);

    const bool signature_compat =
        version_2313::install_early_signature_patch(base);
    if (!signature_compat) {
        LOGE("init: exact 23.1.3 APK re-sign compatibility was not installed; "
             "startup remains fail-closed");
    }

    bool resolved = false;
    for (int i = 0; i < kWaitSteps && !resolved; ++i) {
        resolved = il2cpp::resolve();
        if (!resolved) usleep(kWaitStepUs);
    }
    if (!resolved) {
        LOGE("init: required il2cpp_* exports were not resolved");
        return nullptr;
    }
    LOGI("init: [2/6] IL2CPP API resolved");

    // 23.1.3's il2cpp_domain_get() crashes if used as a pre-init poll. Wait
    // on the exact build's validated root-domain publication slot instead.
    void* domain = il2cpp_runtime_2313::wait_for_domain(
        base, kWaitSteps, kWaitStepUs);
    if (domain == nullptr) {
        LOGE("init: IL2CPP root domain did not become safely available");
        return nullptr;
    }
    LOGI("init: [3/6] domain=%p", domain);

    size_t last = 0u;
    int stable = 0;
    for (int i = 0; i < kWaitSteps && stable < kStableChecks; ++i) {
        const size_t now = assembly_count(domain);
        stable = (now != 0u && now == last) ? stable + 1 : 0;
        last = now;
        usleep(kWaitStepUs);
    }
    if (stable < kStableChecks) {
        LOGE("init: assembly list never settled (last count=%zu)", last);
        return nullptr;
    }
    LOGI("init: [4/6] assembly list settled at %zu assemblies", last);
    usleep(kSettleUs);

    void* attached_thread = il2cpp::thread_attach(domain);
    if (attached_thread == nullptr) {
        LOGE("init: il2cpp_thread_attach failed");
        return nullptr;
    }
    LOGI("init: [5/6] thread attached to runtime");

    void* image = nullptr;
    for (int i = 0; i < kWaitSteps && image == nullptr; ++i) {
        image = il2cpp::find_image("Assembly-CSharp.dll");
        if (image == nullptr) usleep(kWaitStepUs);
    }
    if (image == nullptr) {
        LOGE("init: Assembly-CSharp.dll never appeared; nothing to hook");
        if (il2cpp::thread_detach != nullptr) il2cpp::thread_detach(attached_thread);
        return nullptr;
    }
    LOGI("init: [6/6] Assembly-CSharp.dll ready; installing 23.1.3 hooks");

    const bool version_traces  = version_2313::install_runtime_hooks();
    const bool startup_guards  = startup_guards_2313::install_hooks();
    const bool switcher_trace  = startup_trace_2313::install_hooks();
    const bool local_backend   = backend_local_2313::install_hooks();
    const bool photon_online   = photon_2313::install_hooks();
    const bool default_plugin  = photon_default_plugin_2313::install_hooks();
    const bool photon_trace    = photon_trace_2313::install_hooks();
    if (signature_compat && version_traces && startup_guards &&
        switcher_trace && local_backend && photon_online &&
        default_plugin && photon_trace) {
        LOGI("init: 23.1.3 ARM64 local session + Photon Cloud port armed — "
             "retired update/network modals are disabled, EU/Default "
             "plugin route is active, Switcher heartbeat tracing is on");
    } else {
        LOGE("init: 23.1.3 port incomplete: signature=%d traces=%d "
             "startup-guards=%d switcher-trace=%d local-backend=%d "
             "photon=%d plugin=%d photon-trace=%d",
             signature_compat ? 1 : 0, version_traces ? 1 : 0,
             startup_guards ? 1 : 0, switcher_trace ? 1 : 0,
             local_backend ? 1 : 0, photon_online ? 1 : 0,
             default_plugin ? 1 : 0, photon_trace ? 1 : 0);
    }

    if (il2cpp::thread_detach != nullptr) il2cpp::thread_detach(attached_thread);
    LOGI("init: 23.1.3 bootstrap initialization finished cleanly");
    return nullptr;
}

} // namespace

__attribute__((constructor)) static void on_load() {
    obb_provisioner::provision();
    pthread_t thread;
    if (pthread_create(&thread, nullptr, init_thread, nullptr) == 0) {
        pthread_detach(thread);
    } else {
        LOGE("init: pthread_create failed");
    }
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) {
    obb_provisioner::provision();
    return JNI_VERSION_1_6;
}
