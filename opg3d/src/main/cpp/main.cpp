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
#include "log.h"
#include "obb_provisioner.h"
#include "photon_hooks.h"
#include "player_boost.h"
#include "post_match_ui.h"
#include "removed_arsenal.h"

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
    // Printed first so a report can be matched to the exact library that
    // produced it. A missing or older stamp means the device is running a
    // stale libopg3d.so and no runtime conclusion should be drawn from the
    // rest of the log.
    LOGI("init: libopg3d build %s", OPG3D_BUILD_STAMP);
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

    // Every target below was verified against the supplied 13.2.1 metadata
    // dump. Runtime resolution remains metadata-driven and fail-closed; no RVA
    // from the analysis files is compiled into the library.
    const bool photon_installed = photon::install_hooks();
    const bool guard_installed = cloud_guard::install_hooks();
    const bool boost_installed = player_boost::install_hooks();
    const bool gameplay_installed = legacy_gameplay::install_hooks();
    const bool post_match_installed = post_match_ui::install_hooks();
    const bool free_details_installed = free_detail_weapons::install_hooks();
    const bool clan_craft_installed = clan_craft::install_hooks();
    const bool removed_arsenal_installed = removed_arsenal::install_hooks();
    const bool abuse_slot_sink_installed = abuse_slot_sink::install_hooks();
    const bool cheat_guard_installed = cheat_guard::install_hooks();
    if (photon_installed && guard_installed && boost_installed &&
        gameplay_installed && post_match_installed &&
        free_details_installed && clan_craft_installed &&
        removed_arsenal_installed && abuse_slot_sink_installed &&
        cheat_guard_installed) {
        LOGI("init: phase 0 ready — Photon Cloud routing, progression grant, "
             "tutorial skip, direct post-match player/team table, free detail "
             "weapons, clan-free blueprint crafting, retired arsenal in the "
             "shop, the whole lobby craft catalogue on the account, upgrade "
             "timers, the virtual abuse-slot persistence sink and the local "
             "cheat-detection progress-wipe block active");
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
        if (!gameplay_installed) {
            LOGE("init: legacy gameplay module incomplete; tutorial skip or "
                 "upgrade time may be unavailable");
        }
        if (!post_match_installed) {
            LOGE("init: post-match reward bypass incomplete; the second-round "
                 "OK button can still remain behind a stale one-shot guard");
        }
        if (!free_details_installed) {
            LOGE("init: free detail-weapon compatibility is unavailable");
        }
        if (!clan_craft_installed) {
            LOGE("init: clan blueprints remain locked; the craft-section "
                 "availability gate could not be answered");
        }
        if (!removed_arsenal_installed) {
            LOGE("init: retired arsenal weapons stay hidden; the shop shelf "
                 "builder could not be wrapped");
        }
        if (!abuse_slot_sink_installed) {
            LOGE("init: virtual abuse-slot sink unavailable; battle one can "
                 "still seed a false abuse timestamp for battle two");
        }
        if (!cheat_guard_installed) {
            LOGE("init: the local CHEAT DETECTED punishment is NOT blocked; "
                 "the client can still erase local progress");
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
    // Expansion file first, and synchronously.
    //
    // Unity decides whether its data exists while libunity/libil2cpp
    // initialise, so the copy cannot be deferred to the phase-0 thread below —
    // by the time that thread finds libil2cpp.so the game has already looked
    // for the .obb. This constructor runs while the APK's native libraries are
    // still being loaded, which is the last moment where the file can still
    // appear "before" the engine. On a first launch the loader thread
    // therefore blocks for one sequential copy out of the APK; afterwards the
    // call only stats a single file. Details and failure modes are documented
    // in obb_provisioner.h. A failure here never affects the hooks below: the
    // module only logs and returns false.
    obb_provisioner::provision();

    pthread_t thread;
    if (pthread_create(&thread, nullptr, init_thread, nullptr) == 0) {
        pthread_detach(thread);
    } else {
        LOGE("init: pthread_create failed");
    }
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) {
    // Defence in depth. JNI_OnLoad normally runs after our ELF constructor and
    // finds the work already done (provision() is idempotent), but if this
    // library is ever loaded through a path that reaches JNI_OnLoad first, the
    // expansion file still lands before Unity starts.
    obb_provisioner::provision();
    return JNI_VERSION_1_6;
}
