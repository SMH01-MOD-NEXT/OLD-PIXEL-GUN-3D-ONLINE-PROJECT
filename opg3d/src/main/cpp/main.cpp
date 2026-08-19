#include <cinttypes>
#include <cstdint>

#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include "elf_sym.h"
#include "il2cpp.h"
#include "log.h"
#include "photon_hooks.h"

namespace {

constexpr const char* kIl2Cpp = "libil2cpp.so";
constexpr int kWaitSteps = 6000;
constexpr useconds_t kWaitStepUs = 10 * 1000;

void* init_thread(void*) {
    LOGI("init: phase 0 thread started");

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
    LOGI("init: %s found, base=0x%" PRIxPTR, kIl2Cpp, base);

    bool resolved = false;
    for (int i = 0; i < kWaitSteps && !resolved; ++i) {
        resolved = il2cpp::resolve();
        if (!resolved) usleep(kWaitStepUs);
    }
    if (!resolved) {
        LOGE("init: required il2cpp_* exports were not resolved");
        return nullptr;
    }
    LOGI("init: IL2CPP API resolved");

    void* domain = nullptr;
    bool ready = false;
    for (int i = 0; i < kWaitSteps && !ready; ++i) {
        domain = il2cpp::domain_get();
        ready = domain != nullptr &&
                il2cpp::find_image("Assembly-CSharp.dll") != nullptr;
        if (!ready) usleep(kWaitStepUs);
    }
    if (!ready) {
        LOGE("init: IL2CPP domain/Assembly-CSharp.dll did not become ready");
        return nullptr;
    }

    void* attached_thread = il2cpp::thread_attach(domain);
    if (attached_thread == nullptr) {
        LOGE("init: il2cpp_thread_attach failed");
        return nullptr;
    }
    LOGI("init: runtime ready; installing independent open-source hooks");

    const bool installed = photon::install_hooks();
    if (installed) {
        LOGI("init: phase 0 ready — AppID selection override and connection tracing active");
    } else {
        LOGE("init: core SelectPhotonAppId hook failed; fail-closed, no unsafe RVA patching attempted");
    }

    if (il2cpp::thread_detach != nullptr) {
        il2cpp::thread_detach(attached_thread);
    }
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
