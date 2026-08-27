#include <cinttypes>
#include <cstddef>
#include <cstdint>

#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include "assets_data_2313.h"
#include "backend_emu_2313.h"
#include "backend_local_2313.h"
#include "battle_ui_2313.h"
#include "bots_trace_2313.h"
#include "config.h"
#include "crafting_2313.h"
#include "elf_sym.h"
#include "hidden_items_2313.h"
#include "identity_2313.h"
#include "il2cpp.h"
#include "il2cpp_runtime_2313.h"
#include "live_content_2313.h"
#include "loading_stall_guard_2313.h"
#include "lobby_catalog_2313.h"
#include "log.h"
#include "net_stall_guard_2313.h"
#include "obb_provisioner.h"
#include "online_state_2313.h"
#include "photon_2313.h"
#include "photon_default_plugin_2313.h"
#include "photon_trace_2313.h"
#include "pixel_pass_2313.h"
#include "post_match_2313.h"
#include "progression_2313.h"
#include "rank_ui_2313.h"
#include "startup_guards_2313.h"
#include "startup_trace_2313.h"
#include "version_2313.h"
#include "weapon_modules_2313.h"

namespace {

constexpr const char* kIl2Cpp = "libil2cpp.so";
constexpr int kWaitSteps = 6000;
constexpr useconds_t kWaitStepUs = 10 * 1000;
constexpr int kStableChecks = 25;
constexpr useconds_t kSettleUs = 100 * 1000;

// ============================================================================
// FEATURE CONFIGURATION
// Change only true/false values in this block to isolate a component. Disabled
// components are treated as intentionally skipped, not as initialization
// failures. Keep dependency notes in mind when disabling a driver component.
// ============================================================================
namespace feature_config {

// Startup and diagnostics.
namespace startup {
constexpr bool signature_compatibility = true;
constexpr bool version_traces = true;
constexpr bool startup_guards = true;
constexpr bool switcher_trace = true;
constexpr bool loading_stall_watchdog = true;
constexpr bool obb_provisioner = true;
} // namespace startup

// Local backend and multiplayer/network behavior.
namespace network {
constexpr bool local_backend = true;
constexpr bool suppress_technical_works = true;
constexpr bool backend_emulator = true;
constexpr bool photon_online = true;
constexpr bool photon_default_plugin = true;
constexpr bool photon_trace = true;
constexpr bool network_stall_guard = true;
constexpr bool force_online_state = true;
} // namespace network

// Progression, economy and inventory.
namespace progression {
constexpr bool component = true;
constexpr bool currency = true;
constexpr bool xp = true;
constexpr bool fast_level_road = true;
// These pumps use progression's MainMenuController.Update hook as their driver.
constexpr bool inventory_pumps = true;
constexpr bool crafting = true;
constexpr bool lobby_catalog = true;
constexpr bool live_content = true;
constexpr bool live_content_diagnostics = true;
constexpr bool weapon_modules = true;
constexpr bool hidden_items = true;
constexpr bool pixel_pass = true;
constexpr bool pixel_pass_diagnostics = true;
} // namespace progression

// Player identity and bundled data.
namespace player_content {
constexpr bool local_identity = true;
constexpr bool assets_data = true;
} // namespace player_content

// Battle presentation and gameplay adjustments.
namespace gameplay {
constexpr bool battle_ui = true;
constexpr bool rank_ui = true;
constexpr bool post_match = true;
constexpr bool high_tier_bots = true;
} // namespace gameplay

} // namespace feature_config

template <typename Installer>
bool install_component(const char* category, const char* name, bool enabled,
                       Installer installer) {
    if (!enabled) {
        LOGI("config: [%s] %s disabled", category, name);
        return true;
    }
    const bool installed = installer();
    LOGI("config: [%s] %s %s", category, name,
         installed ? "enabled" : "failed");
    return installed;
}

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

    const bool signature_compat = install_component(
        "startup", "signature compatibility",
        feature_config::startup::signature_compatibility,
        [base]() { return version_2313::install_early_signature_patch(base); });
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

    const progression_2313::Options progression_options{
        feature_config::progression::currency,
        feature_config::progression::xp,
        feature_config::progression::fast_level_road,
        feature_config::progression::inventory_pumps,
        feature_config::progression::live_content_diagnostics,
        feature_config::progression::pixel_pass_diagnostics,
    };

    const bool version_traces = install_component(
        "startup", "version traces", feature_config::startup::version_traces,
        []() { return version_2313::install_runtime_hooks(); });
    const bool startup_guards = install_component(
        "startup", "startup guards", feature_config::startup::startup_guards,
        []() { return startup_guards_2313::install_hooks(); });
    const bool switcher_trace = install_component(
        "startup", "switcher trace", feature_config::startup::switcher_trace,
        []() { return startup_trace_2313::install_hooks(); });
    const bool stall_watchdog = install_component(
        "startup", "loading stall watchdog",
        feature_config::startup::loading_stall_watchdog,
        []() { return loading_stall_guard_2313::start_watchdog(); });

    const bool local_identity = install_component(
        "player-content", "local identity",
        feature_config::player_content::local_identity,
        []() { return identity_2313::install_hooks(); });

    const bool local_backend = install_component(
        "network", "local backend", feature_config::network::local_backend,
        []() {
            return backend_local_2313::install_hooks(
                feature_config::network::suppress_technical_works);
        });
    const bool backend_emu = install_component(
        "network", "backend emulator", feature_config::network::backend_emulator,
        []() { return backend_emu_2313::install_hooks(); });
    const bool photon_online = install_component(
        "network", "Photon online", feature_config::network::photon_online,
        []() { return photon_2313::install_hooks(); });
    const bool default_plugin = install_component(
        "network", "Photon default plugin",
        feature_config::network::photon_default_plugin,
        []() { return photon_default_plugin_2313::install_hooks(); });
    const bool photon_trace = install_component(
        "network", "Photon trace", feature_config::network::photon_trace,
        []() { return photon_trace_2313::install_hooks(); });
    const bool net_stall = install_component(
        "network", "network stall guard",
        feature_config::network::network_stall_guard,
        []() { return net_stall_guard_2313::install_hooks(); });
    const bool online_state = install_component(
        "network", "forced online state",
        feature_config::network::force_online_state,
        []() { return online_state_2313::install_hooks(); });

    const bool progression = install_component(
        "progression", "core/currency/xp",
        feature_config::progression::component,
        [base, &progression_options]() {
            return progression_2313::install_hooks(base, progression_options);
        });
    const bool crafting = install_component(
        "progression", "crafting", feature_config::progression::crafting,
        []() { return crafting_2313::install_hooks(); });
    const bool lobby_catalog = install_component(
        "progression", "lobby catalog",
        feature_config::progression::lobby_catalog,
        []() { return lobby_catalog_2313::install_hooks(); });
    const bool live_content = install_component(
        "progression", "live content", feature_config::progression::live_content,
        [base]() { return live_content_2313::install_hooks(base); });
    const bool weapon_modules = install_component(
        "progression", "weapon modules",
        feature_config::progression::weapon_modules,
        [base]() { return weapon_modules_2313::install_hooks(base); });
    const bool hidden_items = install_component(
        "progression", "hidden items", feature_config::progression::hidden_items,
        [base]() { return hidden_items_2313::install_hooks(base); });
    const bool pixel_pass = install_component(
        "progression", "Pixel Pass", feature_config::progression::pixel_pass,
        [base]() { return pixel_pass_2313::install_hooks(base); });

    const bool assets_payload = install_component(
        "player-content", "assets/data payload",
        feature_config::player_content::assets_data,
        [base]() { return assets_data_2313::install_hooks(base); });

    const bool battle_ui = install_component(
        "gameplay", "battle UI", feature_config::gameplay::battle_ui,
        [base]() { return battle_ui_2313::install_hooks(base); });
    const bool rank_ui = install_component(
        "gameplay", "rank UI", feature_config::gameplay::rank_ui,
        []() { return rank_ui_2313::install_hooks(); });
    const bool post_match = install_component(
        "gameplay", "post match", feature_config::gameplay::post_match,
        [base]() { return post_match_2313::install_hooks(base); });
    const bool bots_trace = install_component(
        "gameplay", "high-tier bots", feature_config::gameplay::high_tier_bots,
        []() { return bots_trace_2313::install_hooks(); });
    if (signature_compat && version_traces && startup_guards &&
        switcher_trace && stall_watchdog && local_backend && photon_online &&
        default_plugin && photon_trace && progression && crafting &&
        lobby_catalog && live_content && weapon_modules && hidden_items &&
        local_identity && assets_payload && net_stall && post_match &&
        online_state && battle_ui && rank_ui && bots_trace && backend_emu &&
        pixel_pass) {
        LOGI("init: all enabled 23.1.3 components installed; "
             "disabled components were intentionally skipped");
    } else {
        LOGE("init: 23.1.3 port incomplete: signature=%d traces=%d "
             "startup-guards=%d switcher-trace=%d stall-watchdog=%d "
             "local-backend=%d photon=%d plugin=%d photon-trace=%d "
             "progression=%d crafting=%d lobby-catalog=%d live-content=%d "
             "modules=%d "
             "hidden-items=%d pixel-pass=%d "
             "identity=%d assets-data=%d net-stall=%d "
             "post-match=%d online-state=%d battle-ui=%d rank-ui=%d "
             "bots=%d backend-emu=%d",
             signature_compat ? 1 : 0, version_traces ? 1 : 0,
             startup_guards ? 1 : 0, switcher_trace ? 1 : 0,
             stall_watchdog ? 1 : 0, local_backend ? 1 : 0,
             photon_online ? 1 : 0, default_plugin ? 1 : 0,
             photon_trace ? 1 : 0, progression ? 1 : 0,
             crafting ? 1 : 0, lobby_catalog ? 1 : 0, live_content ? 1 : 0,
             weapon_modules ? 1 : 0,
             hidden_items ? 1 : 0, pixel_pass ? 1 : 0,
             local_identity ? 1 : 0, assets_payload ? 1 : 0,
             net_stall ? 1 : 0, post_match ? 1 : 0,
             online_state ? 1 : 0, battle_ui ? 1 : 0, rank_ui ? 1 : 0,
             bots_trace ? 1 : 0, backend_emu ? 1 : 0);
    }

    if (il2cpp::thread_detach != nullptr) il2cpp::thread_detach(attached_thread);
    LOGI("init: 23.1.3 bootstrap initialization finished cleanly");
    return nullptr;
}

} // namespace

__attribute__((constructor)) static void on_load() {
    if (feature_config::startup::obb_provisioner) {
        obb_provisioner::provision();
    }
    pthread_t thread;
    if (pthread_create(&thread, nullptr, init_thread, nullptr) == 0) {
        pthread_detach(thread);
    } else {
        LOGE("init: pthread_create failed");
    }
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM*, void*) {
    if (feature_config::startup::obb_provisioner) {
        obb_provisioner::provision();
    }
    return JNI_VERSION_1_6;
}
