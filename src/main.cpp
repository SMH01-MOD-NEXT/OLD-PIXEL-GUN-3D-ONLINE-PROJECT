#include <cstdint>
#include <cstring>
#include <cinttypes>

#include <jni.h>
#include <link.h>
#include <pthread.h>
#include <unistd.h>

#include <shadowhook.h>

#include "il2cpp.h"
#include "log.h"
#include "photon_hook.h"

// Точка входа: при загрузке библиотеки (constructor / System.loadLibrary)
// поднимаем фоновый поток: ждём libil2cpp.so, резолвим il2cpp API, ставим хук.
// Логи — строго по одному сообщению на смену состояния, без спама в циклах.
namespace {

constexpr int kWaitIterations = 600;          // 600 * 100мс = 60 сек на этап
constexpr useconds_t kWaitStepUs = 100 * 1000;

int phdr_callback(struct dl_phdr_info* info, size_t, void* data) {
    if (info->dlpi_name && std::strstr(info->dlpi_name, "libil2cpp.so")) {
        *reinterpret_cast<uintptr_t*>(data) = static_cast<uintptr_t>(info->dlpi_addr);
        return 1;
    }
    return 0;
}

uintptr_t find_il2cpp_base() {
    uintptr_t base = 0;
    dl_iterate_phdr(phdr_callback, &base);
    return base;
}

void* init_thread(void*) {
    LOGI("init: поток запущен");

    // ShadowHook нужен уже на этапе резолва символов: shadowhook_dlopen/dlsym
    // ходят по solist линкера напрямую и пробивают namespace'ы Android 7+
    // (dlsym(RTLD_DEFAULT) libil2cpp.so НЕ видит — она грузится RTLD_LOCAL).
    int rc = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    if (rc != 0) {
        LOGE("init: shadowhook_init failed: %d (%s)", rc, shadowhook_to_errmsg(rc));
        return nullptr;
    }

    // Этап 1: ждём libil2cpp.so в процессе.
    uintptr_t base = 0;
    for (int i = 0; i < kWaitIterations && base == 0; ++i) {
        base = find_il2cpp_base();
        if (base == 0) usleep(kWaitStepUs);
    }
    if (base == 0) {
        LOGE("init: libil2cpp.so НЕ НАЙДЕНА в процессе за %d сек — "
             "библиотека не загружена в эту игру", kWaitIterations / 10);
        return nullptr;
    }
    LOGI("init: libil2cpp.so найдена, base = 0x%" PRIxPTR, base);

    // Этап 2: handle на уже загруженную libil2cpp.so.
    void* handle = nullptr;
    for (int i = 0; i < kWaitIterations && !handle; ++i) {
        handle = shadowhook_dlopen("libil2cpp.so");
        if (!handle) usleep(kWaitStepUs);
    }
    if (!handle) {
        LOGE("init: libil2cpp.so найдена по maps, но shadowhook_dlopen не смог взять handle");
        return nullptr;
    }

    // Этап 3: резолвим il2cpp API + ждём готовности домена и Assembly-CSharp.
    bool resolved = false;
    bool ready = false;
    for (int i = 0; i < kWaitIterations; ++i) {
        resolved = il2cpp::resolve(handle);
        if (resolved) {
            ready = il2cpp::domain_get() != nullptr
                 && il2cpp::find_image("Assembly-CSharp.dll") != nullptr;
            if (ready) break;
        }
        usleep(kWaitStepUs);
    }
    if (!resolved) {
        LOGE("init: il2cpp_* символы не зарезолвились за %d сек (handle есть!) — "
             "похоже, в этой сборке игры нет экспорта il2cpp API", kWaitIterations / 10);
        return nullptr;
    }
    if (!ready) {
        LOGE("init: IL2CPP-домен/Assembly-CSharp не поднялись за %d сек", kWaitIterations / 10);
        return nullptr;
    }
    LOGI("init: il2cpp API готов");

    // Этап 4: хук.
    if (photon::install_hook(reinterpret_cast<void*>(base))) {
        LOGI("init: всё готово, ждём вызова ConnectUsingSettings");
    }
    return nullptr;
}

} // namespace

__attribute__((constructor)) static void on_load() {
    pthread_t t;
    if (pthread_create(&t, nullptr, init_thread, nullptr) == 0) {
        pthread_detach(t);
    } else {
        LOGE("init: pthread_create failed");
    }
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) {
    // На случай загрузки через System.loadLibrary — constructor уже всё сделал.
    return JNI_VERSION_1_6;
}
