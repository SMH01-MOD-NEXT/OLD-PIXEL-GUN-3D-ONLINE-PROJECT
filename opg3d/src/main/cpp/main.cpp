#include <cinttypes>
#include <cstdint>

#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include "elf_sym.h"
#include "il2cpp.h"
#include "log.h"
#include "photon_patch.h"

// Точка входа: при загрузке библиотеки поднимаем фоновый поток:
// ждём libil2cpp.so -> резолвим il2cpp API -> ждём рантайм -> держим подмену
// настроек Photon. Логи — строго по одному сообщению на смену состояния.
//
// Инлайн-хук здесь сознательно не используется: мы не меняем код игры, а только
// данные (поля объекта настроек), а значит не нужны ни перехват функций, ни
// патчинг линкера, ни перехват сигналов — то есть нечему падать при инициализации.
namespace {

constexpr const char* kIl2Cpp = "libil2cpp.so";

constexpr int kWaitSteps = 600;                   // 600 * 100мс = 60 сек на этап
constexpr useconds_t kWaitStepUs = 100 * 1000;
constexpr useconds_t kPollFastUs = 50 * 1000;     // до первого применения
constexpr useconds_t kPollSlowUs = 500 * 1000;    // после — просто сторожим

void* init_thread(void*) {
    LOGI("init: поток запущен");

    // 1. Ждём, пока игра загрузит свой IL2CPP-образ.
    uintptr_t base = 0;
    bool found = false;
    for (int i = 0; i < kWaitSteps && !found; ++i) {
        found = elfsym::find_library(kIl2Cpp, &base);
        if (!found) usleep(kWaitStepUs);
    }
    if (!found) {
        LOGE("init: %s НЕ НАЙДЕНА в процессе за %d сек — библиотека не загружена в эту игру",
             kIl2Cpp, kWaitSteps / 10);
        return nullptr;
    }
    LOGI("init: %s найдена, base = 0x%" PRIxPTR, kIl2Cpp, base);

    // 2. Резолвим экспорты чтением .dynsym из памяти (dlsym тут бесполезен).
    bool resolved = false;
    for (int i = 0; i < kWaitSteps && !resolved; ++i) {
        resolved = il2cpp::resolve();
        if (!resolved) usleep(kWaitStepUs);
    }
    if (!resolved) {
        LOGE("init: экспорты il2cpp_* не найдены в .dynsym за %d сек — неподдерживаемая сборка игры",
             kWaitSteps / 10);
        return nullptr;
    }
    LOGI("init: экспорты il2cpp найдены");

    // 3. Ждём рантайм и игровую сборку.
    void* domain = nullptr;
    bool ready = false;
    for (int i = 0; i < kWaitSteps && !ready; ++i) {
        domain = il2cpp::domain_get();
        ready = (domain != nullptr) && (il2cpp::find_image("Assembly-CSharp.dll") != nullptr);
        if (!ready) usleep(kWaitStepUs);
    }
    if (!ready) {
        LOGE("init: IL2CPP-домен/Assembly-CSharp не поднялись за %d сек", kWaitSteps / 10);
        return nullptr;
    }

    // 4. Регистрируем свой поток в рантайме — без этого нельзя создавать
    // managed-строки (GC просто не знает про чужой поток).
    if (il2cpp::thread_attach(domain) == nullptr) {
        LOGE("init: il2cpp_thread_attach не сработал");
        return nullptr;
    }
    LOGI("init: рантайм готов, поток присоединён");

    // 5. Сторожим настройки: применяем сразу, как они появятся, и возвращаем
    // подмену, если игра перезагрузит ассет.
    bool applied_once = false;
    bool said_waiting = false;
    bool said_noclass = false;
    int reapplies = 0;

    for (;;) {
        switch (photon::apply()) {
            case photon::ApplyResult::Applied:
                if (!applied_once) {
                    applied_once = true;
                    LOGI("init: всё готово — настройки Photon подменены");
                } else {
                    ++reapplies;
                    LOGI("watch: игра перезагрузила настройки, подмена возвращена (#%d)", reapplies);
                }
                break;

            case photon::ApplyResult::NotReady:
                if (!said_waiting) {
                    said_waiting = true;
                    LOGI("watch: ждём, когда игра создаст PhotonServerSettings");
                }
                break;

            case photon::ApplyResult::NoClass:
                if (!said_noclass) {
                    said_noclass = true;
                    LOGE("watch: класс PhotonNetwork или поле PhotonServerSettings не найдены");
                }
                break;

            case photon::ApplyResult::AlreadyApplied:
                break;
        }

        usleep(applied_once ? kPollSlowUs : kPollFastUs);
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
