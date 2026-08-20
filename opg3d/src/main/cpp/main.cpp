#include <cinttypes>
#include <cstddef>
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

// il2cpp_domain_get() начинает отдавать домен задолго до конца il2cpp_init:
// сборки главный поток регистрирует уже после этого момента. При этом
// il2cpp_domain_get_assemblies() возвращает указатель прямо на внутренний
// вектор рантайма, поэтому обход списка во время регистрации читает
// перевыделенную (то есть уже освобождённую) память и роняет наш фоновый
// поток. Ждём, пока состав сборок перестанет меняться, и только потом трогаем
// метаданные.
constexpr int kStableChecks = 25;            // ~250 мс одинаковых замеров подряд
constexpr useconds_t kSettleUs = 750 * 1000; // запас после стабилизации

// Безопасный опрос: берём только количество сборок и не разыменовываем сам
// массив, поэтому гонка с регистрацией не приводит к чтению чужой памяти.
size_t assembly_count(void* domain) {
    if (domain == nullptr || il2cpp::domain_get_assemblies == nullptr) return 0u;
    size_t count = 0u;
    il2cpp::domain_get_assemblies(domain, &count);
    if (count > 8192u) return 0u; // рваное чтение — считаем, что ещё не готово
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

    // Присоединяемся к рантайму ДО любой работы с метаданными: обход сборок и
    // il2cpp_class_from_name() трогают структуры, которыми управляет GC.
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

    const bool installed = photon::install_hooks();
    if (installed) {
        LOGI("init: phase 0 ready — AppID selection override and connection tracing active");
    } else {
        LOGE("init: core SelectPhotonAppId hook failed; fail-closed, no unsafe RVA patching attempted");
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
