#include <cstdint>
#include <cstring>
#include <cinttypes>

#include <dlfcn.h>
#include <jni.h>
#include <link.h>
#include <pthread.h>
#include <unistd.h>

#include <shadowhook.h>

#include "il2cpp.h"
#include "log.h"
#include "photon_hook.h"

// Точка входа: при загрузке библиотеки (constructor / System.loadLibrary)
// поднимаем фоновый поток, который ждёт появления libil2cpp.so и готовности
// IL2CPP-домена, затем ставит хук. Библиотека может быть подгружена как до,
// так и после старта игры — поэтому всё через циклы ожидания.
namespace {

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

    // 1. Ждём libil2cpp.so в адресном пространстве (до 60 сек).
    uintptr_t base = 0;
    for (int i = 0; i < 600 && base == 0; ++i) {
        base = find_il2cpp_base();
        if (base == 0) usleep(100 * 1000);
    }
    if (base == 0) {
        LOGE("init: libil2cpp.so не появился в процессе за 60 сек");
        return nullptr;
    }
    LOGI("init: libil2cpp.so base = 0x%" PRIxPTR, base);

    // 2. Ждём, пока поднимется IL2CPP-домен и загрузится Assembly-CSharp (до 60 сек).
    bool ready = false;
    for (int i = 0; i < 600 && !ready; ++i) {
        ready = il2cpp::resolve()
            && il2cpp::domain_get != nullptr
            && il2cpp::domain_get() != nullptr
            && il2cpp::find_image("Assembly-CSharp.dll") != nullptr;
        if (!ready) usleep(100 * 1000);
    }
    if (!ready) {
        LOGE("init: IL2CPP-домен не инициализировался за 60 сек");
        return nullptr;
    }
    LOGI("init: IL2CPP готов");

    // 3. ShadowHook (одна хук-библиотека в процессе -> MODE_UNIQUE).
    int rc = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    if (rc != 0) {
        LOGE("init: shadowhook_init failed: %d (%s)", rc, shadowhook_to_errmsg(rc));
        return nullptr;
    }

    // 4. Хук.
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
