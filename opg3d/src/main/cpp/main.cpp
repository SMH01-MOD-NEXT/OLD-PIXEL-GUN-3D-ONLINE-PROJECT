#include <cinttypes>
#include <cstddef>
#include <cstdint>

#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include "abuse_slot_sink.h"
#include "cheat_guard.h"
#include "clan_craft.h"
#include "cloud_guard.h"
#include "config.h"
#include "elf_sym.h"
#include "free_detail_weapons.h"
#include "il2cpp.h"
#include "legacy_gameplay.h"
#include "lobby_catalog_1411.h"
#include "log.h"
#include "obb_provisioner.h"
#include "photon_hooks.h"
#include "player_boost.h"
#include "post_match_ui.h"
#include "removed_arsenal.h"
#include "version_1411.h"

namespace {

constexpr const char* kIl2Cpp = "libil2cpp.so";
constexpr int kWaitSteps = 6000;
constexpr useconds_t kWaitStepUs = 10 * 1000;
constexpr int kStableChecks = 25;
constexpr useconds_t kSettleUs = 750 * 1000;

size_t assembly_count(void* domain) {
    if (domain == nullptr || il2cpp::domain_get_assemblies == nullptr) return 0u;
    size_t count = 0u;
    il2cpp::domain_get_assemblies(domain, &count);
    if (count > 8192u) return 0u;
    return count;
}

void* init_thread(void*) {
    LOGI("init: libopg3d build %s", OPG3D_BUILD_STAMP);
    LOGI("init: [0/6] 14.1.1 phase-0 thread started");

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

    // Install before AppsMenu's first Start tick. Ordinary features remain
    // metadata-resolved; this verified instruction is the sole version-bound
    // patch because the signature comparison has no safe managed boundary.
    const bool signature_compat =
        version_1411::install_early_signature_patch(base);
    if (!signature_compat) {
        LOGE("init: 14.1.1 APK re-sign compatibility was not installed; "
             "ClosingScene may still intercept menu transitions");
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
    LOGI("init: [6/6] Assembly-CSharp.dll ready; installing 14.1.1 hooks");

    // Every managed target was revalidated against dump1411.cs. The two API
    // changes are isolated in version_1411.h and lobby_catalog_1411.h; the new
    // reward event is handled by post_match_ui.h.
    const bool photon_installed = photon::install_hooks();
    const bool guard_installed = cloud_guard::install_hooks();
    const bool version_runtime_installed =
        version_1411::install_runtime_hooks();
    const bool boost_installed = player_boost::install_hooks();
    const bool lobby_catalog_installed =
        lobby_catalog_1411::install_hooks();
    const bool gameplay_installed = legacy_gameplay::install_hooks();
    const bool post_match_installed = post_match_ui::install_hooks();
    const bool free_details_installed = free_detail_weapons::install_hooks();
    const bool clan_craft_installed = clan_craft::install_hooks();
    const bool removed_arsenal_installed = removed_arsenal::install_hooks();
    const bool abuse_slot_sink_installed = abuse_slot_sink::install_hooks();
    const bool cheat_guard_installed = cheat_guard::install_hooks();

    if (signature_compat && photon_installed && guard_installed &&
        version_runtime_installed && boost_installed &&
        lobby_catalog_installed && gameplay_installed &&
        post_match_installed && free_details_installed &&
        clan_craft_installed && removed_arsenal_installed &&
        abuse_slot_sink_installed && cheat_guard_installed) {
        LOGI("init: 14.1.1 port ready — APK re-sign compatibility, Photon "
             "Cloud/squad routing, progression and lobby catalogue grants, "
             "tutorial/time compatibility, direct post-match table, free "
             "detail/clan crafting, retired arsenal and anti-punishment "
             "guards active");
    } else {
        if (!signature_compat) {
            LOGE("init: 14.1.1 signature decision patch failed");
        }
        if (!photon_installed) {
            LOGE("init: core SelectPhotonAppId hook failed");
        }
        if (!guard_installed || !version_runtime_installed) {
            LOGE("init: Photon Cloud or 14.1.1 squad routing incomplete");
        }
        if (!boost_installed) {
            LOGE("init: progression grant incomplete");
        }
        if (!lobby_catalog_installed) {
            LOGE("init: 14.1.1 lobby catalogue grant unavailable");
        }
        if (!gameplay_installed) {
            LOGE("init: tutorial skip or local upgrade time unavailable");
        }
        if (!post_match_installed) {
            LOGE("init: 14.1.1 post-match reward bypass incomplete");
        }
        if (!free_details_installed) {
            LOGE("init: free detail-weapon compatibility unavailable");
        }
        if (!clan_craft_installed) {
            LOGE("init: clan blueprint crafting unavailable");
        }
        if (!removed_arsenal_installed) {
            LOGE("init: retired arsenal shelf compatibility unavailable");
        }
        if (!abuse_slot_sink_installed || !cheat_guard_installed) {
            LOGE("init: anti-abuse/punishment guards incomplete; do not use a "
                 "valuable save until the log is reviewed");
        }
    }

    if (il2cpp::thread_detach != nullptr) {
        il2cpp::thread_detach(attached_thread);
    }
    LOGI("init: 14.1.1 phase-0 thread finished cleanly");
    return nullptr;
}

} // namespace

__attribute__((constructor)) static void on_load() {
    // The framework-assisted OBB provisioner derives package/version/file
    // names from the installed APK; it does not hard-code 13.2.1.
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
