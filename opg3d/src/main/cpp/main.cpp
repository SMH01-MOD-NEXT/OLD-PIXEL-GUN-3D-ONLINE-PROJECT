#include <cinttypes>
#include <cstddef>
#include <cstdint>

#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include "cloud_guard.h"
#include "elf_sym.h"
#include "il2cpp.h"
#include "log.h"
#include "photon_hooks.h"
#include "player_boost.h"

namespace {

constexpr const char* kIl2Cpp = "libil2cpp.so";
constexpr int kWaitSteps = 6000;
constexpr useconds_t kWaitStepUs = 10 * 1000;

// il2cpp_domain_get() starts returning a domain long before il2cpp_init
// finishes: the main thread keeps registering assemblies after that point.
// il2cpp_domain_get_assemblies() returns a pointer straight into the runtime's
// internal vector, so walking the list during registration reads reallocated
// (already freed) memory and crashes our background thread. Wait until the
// assembly set stops changing before touching any metadata.
constexpr int kStableChecks = 25;            // ~250 ms of identical samples in a row
constexpr useconds_t kSettleUs = 750 * 1000; // headroom after stabilization

// Race-safe polling: read only the assembly count and never dereference the
// array itself, so the registration race cannot make us read freed memory.
size_t assembly_count(void* domain) {
    if (domain == nullptr || il2cpp::domain_get_assemblies == nullptr) return 0u;
    size_t count = 0u;
    il2cpp::domain_get_assemblies(domain, &count);
    if (count > 8192u) return 0u; // torn read — treat as not ready yet
    return count;
}

void* init_thread(void*) {
    LOGI("init: [0/6] phase 0 thread started");

    uintptr_t base = 0;
    bool found = false;
    for (int i = 0; i < kWaitSteps && !found; ++i) {
        found = elfsym::find_library(kIl2Cpp, &base);
        if (!found) usleep(kWaitStepUs);
    }
    if (!found) {
        LOGE("init: %s not found in process after 60 seconds", kIl2Cpp);
        return nullptr;
    }
    LOGI("init: [1/6] %s found, base=0x%" PRIxPTR, kIl2Cpp, base);

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

    void* domain = nullptr;
    for (int i = 0; i < kWaitSteps && domain == nullptr; ++i) {
        domain = il2cpp::domain_get();
        if (domain == nullptr) usleep(kWaitStepUs);
    }
    if (domain == nullptr) {
        LOGE("init: il2cpp_domain_get() stayed null");
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

    // Attach to the runtime BEFORE any metadata work: walking assemblies and
    // il2cpp_class_from_name() touch GC-owned structures.
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
        if (il2cpp::thread_detach != nullptr) {
            il2cpp::thread_detach(attached_thread);
        }
        return nullptr;
    }
    LOGI("init: [6/6] Assembly-CSharp.dll ready; installing hooks");

    // Install tracing first: cloud_guard intentionally calls the already-hooked
    // ServerSettings.UseCloud(appId, eu) entry so its before/after state is
    // visible in the same diagnostic stream. player_boost is independent and
    // simply shares the same hook engine.
    const bool photon_installed = photon::install_hooks();
    const bool guard_installed = cloud_guard::install_hooks();
    const bool boost_installed = player_boost::install_hooks();
    if (photon_installed && guard_installed && boost_installed) {
        LOGI("init: phase 0 ready — AppID override, Photon Cloud routing, "
             "dead-backend guard, progression grant and connection tracing active");
    } else {
        if (!photon_installed) {
            LOGE("init: core SelectPhotonAppId hook failed; fail-closed, "
                 "no unsafe RVA patching attempted");
        }
        if (!guard_installed) {
            LOGE("init: Photon Cloud/dead-backend guard incomplete; "
                 "do not treat this build as a successful online fix");
        }
        if (!boost_installed) {
            LOGE("init: progression grant incomplete; level/currency grant "
                 "is not active");
        }
    }

    if (il2cpp::thread_detach != nullptr) {
        il2cpp::thread_detach(attached_thread);
    }
    LOGI("init: phase 0 thread finished cleanly");
    return nullptr;
}

} // namespace

__attribute__((constructor)) static void on_load() {
    pthread_t thread;
    if (pthread_create(&thread, nullptr, init_thread, nullptr) == 0) {
        pthread_detach(thread);
    } else {
        LOGE("init: pthread_create failed");
    }
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) {
    return JNI_VERSION_1_6;
}
