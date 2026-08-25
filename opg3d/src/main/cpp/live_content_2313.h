#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) live content unlock: PixelPass (this build's battle pass),
// lotteries / card roulette, chests, the task book and the recurring event
// content.
//
// Why the lobby is empty offline
// ------------------------------
// Every one of those systems is hidden behind a single string-keyed predicate,
// 世丁丒专东专丛一且::一丈丞丞万丐与丏业 ("is this feature open"). The feature ids
// are plain constants in that same class: "feature.pixelpass",
// "feature.battlepass", "feature.roulette", "feature.cardroulette",
// "feature.rouletteads", "feature.threechests", "feature.adschest",
// "feature.taskbook", "feature.eggsdelivery", "feature.piggy", ...
//
// The predicate is not a local flag. Disassembly of the string overload
// (RVA 0x20DF308) shows a lookup in the server-provided ExpOpenSystem table
// followed by a player-level comparison, spread over four entry points that
// fall through into each other:
//
//   020DF308  mov  x19, x0
//   020DF314  bl   0x3664E34  ; ExpOpenSystem.上丘丏丏世丆丌不三::下丌丑丁下丟丛丘上()
//   020DF318  cbz  x0, ...    ; no table at all            -> false
//   020DF324  bl   0x3669480  ; 丂七且丐丗丗一且丛(id) -> 丂丁不丙丅下不丐下 entry
//   020DF32C  b    0x20DF334  ; falls into the entry overload
//   020DF334  cbz  x0, 0x20DF350  ; id is not in the table -> false
//   020DF344  bl   0x3F9DAC8  ; entry.Progress.丞丏三丁丅丗丕三丝() -> unlock level
//   020DF34C  b    0x20DF364  ; falls into the int overload
//   020DF3AC  bl   0x1C79B10  ; ExperienceController::三世丒丄丘下丘丝丅() (level)
//   020DF3B0  cmp  w0, w19    ; player level >= unlock level ?
//
// That table is pure backend content: it arrives with the config payload. The
// emulated backend answers config endpoints with `{}` (see
// docs/PORT_23_1_3_BACKEND_EMULATION.md), so the table is empty, every lookup
// returns null and every content feature reports "closed" before the level
// comparison is even reached. The content itself ships inside the APK — only
// its switchboard is missing. See docs/PORT_23_1_3_LIVE_CONTENT.md.
//
// What this module does
// ---------------------
// It hooks the string overload and answers `true` for a curated list of
// content feature ids when the stock verdict is "closed". Everything else
// falls through to stock, so features that legitimately depend on live server
// state are untouched.
//
// Overload safety. Four overloads share the metadata name and all four take
// exactly one argument, so name plus argument count cannot select one of them:
// the string overload is therefore proven by pointer equality against
// libil2cpp.so base + its recorded RVA (ELF build id
// 57fcc18d2db06212416d480d53c0f881ee47c52a). hook::install() resolves the same
// MethodInfo through the same metadata lookup, so proving the resolved pointer
// proves what gets patched; if it does not match, nothing is armed.
//
// The enum overload (0x20DF354) converts its argument to a name and
// tail-branches into 0x20DF308, so enum-based checks pass through this hook
// too. The entry overload (0x20DF334) and the int overload (0x20DF364) are
// separate entry points past the patched prologue and keep running stock code.
//
// ARM64 ABI reminder: generated managed methods take their explicit arguments
// followed by MethodInfo*; this predicate is static, so the hook signature is
// (managed string, MethodInfo*) -> bool.
// -----------------------------------------------------------------------------

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace live_content_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ------------------------------------------------------------------ tunables

// Frames to wait after the main menu appears before the first report, so the
// profile and the ExpOpenSystem singleton are both live.
constexpr uint64_t kWarmupFrames = 240u;

// Frames between two counter summaries (~1 min at 60 fps).
constexpr uint64_t kReportPeriodFrames = 3600u;

// Distinct feature ids logged at most once each (the predicate is called from
// UI refresh paths, so per-call logging would flood logcat).
constexpr size_t kMaxLoggedIds = 48u;

// ----------------------------------------------------------- metadata names

constexpr const char* kGlobalNs = "";
constexpr const char* kGateClass = "世丁丒专东专丛一且";
constexpr const char* kGateOpen = "一丈丞丞万丐与丏业";

// ---------------------------------------------------------- verified offset
//
// String overload of the feature predicate in the verified 23.1.3 ARM64
// libil2cpp.so. Used as an image *and* overload proof, never as a call target.
constexpr uintptr_t kGateStringRva = 0x20DF308u;

// -------------------------------------------------------------- content ids
//
// Feature ids opened locally when the offline ExpOpenSystem table has no entry
// for them. Every id below drives content that is fully contained in the APK
// and in this port's local systems, so opening it cannot wait on a live
// service. Ids that depend on real server state (clans, squad, friends,
// private matches, mailbox, tournament, brawl, subscriptions) and the
// "gamemode" / "map" families owned by lobby_catalog_2313 are deliberately
// absent — see docs/PORT_23_1_3_LIVE_CONTENT.md.
constexpr const char* kContentFeatures[] = {
    // Battle pass (called PixelPass in this build) and its store tab.
    "feature.pixelpass",
    "feature.battlepass",
    "feature.AdsPixelPassStore",
    // Lotteries: the wheel, the card lottery and their ad-funded spins.
    "feature.roulette",
    "feature.cardroulette",
    "feature.rouletteads",
    // Chest content that the roulette and the event screens hand out.
    "feature.threechests",
    "feature.adschest",
    // Recurring task / event content.
    "feature.taskbook",
    "feature.eggsdelivery",
    "feature.piggy",
    "feature.adscampaign",
    // Collections and workshops this port already grants locally.
    "feature.gallery",
    "feature.pets",
    "feature.craft",
    "feature.modules",
    "feature.trader",
    "feature.loadout",
    "feature.thirdloadout",
    "feature.weapon_info_banner",
    "feature.misc",
};
constexpr size_t kContentFeatureCount =
    sizeof(kContentFeatures) / sizeof(kContentFeatures[0]);

// ------------------------------------------------------------- managed ABI

using GateFn = bool (*)(void* feature_id, void* method);

// ------------------------------------------------------------------- state

inline void* g_gate_info = nullptr;
inline GateFn g_orig_gate = nullptr;
inline uint64_t g_frames = 0u;
inline uint64_t g_queries = 0u;
inline uint64_t g_stock_open = 0u;
inline uint64_t g_opened = 0u;
inline std::string g_logged[kMaxLoggedIds];
inline size_t g_logged_count = 0u;
inline bool g_reported = false;
inline bool g_installed = false;

// ------------------------------------------------------------- diagnostics

inline bool is_content_feature(const std::string& id) {
    for (size_t i = 0u; i < kContentFeatureCount; ++i) {
        if (id == kContentFeatures[i]) return true;
    }
    return false;
}

// True the first time this id is seen, so every opened feature is logged once.
inline bool first_sight(const std::string& id) {
    for (size_t i = 0u; i < g_logged_count; ++i) {
        if (g_logged[i] == id) return false;
    }
    if (g_logged_count < kMaxLoggedIds) g_logged[g_logged_count++] = id;
    return true;
}

// ---------------------------------------------------------------- the hook

inline bool gate_hook(void* feature_id, void* method) {
    const bool stock =
        g_orig_gate != nullptr ? g_orig_gate(feature_id, method) : false;
    ++g_queries;
    if (stock) {
        ++g_stock_open;
        return true;
    }
    if (feature_id == nullptr) return stock;

    const std::string id = il2cpp::to_utf8(feature_id, 64u);
    if (id.empty() || !is_content_feature(id)) return stock;

    ++g_opened;
    if (first_sight(id)) {
        LOGI("23.1.3-content: '%s' is closed in the offline ExpOpenSystem "
             "table; opened locally", id.c_str());
    }
    return true;
}

// One-shot proof of the root cause: asks the *stock* predicate about every id
// this module curates. Offline that count is expected to be 0, which is
// exactly why the lobby had no battle pass, no lottery and no tasks.
inline void report_stock_state() {
    if (g_orig_gate == nullptr || g_gate_info == nullptr ||
        il2cpp::string_new == nullptr) {
        return;
    }

    size_t open = 0u;
    for (size_t i = 0u; i < kContentFeatureCount; ++i) {
        void* managed = il2cpp::string_new(kContentFeatures[i]);
        if (managed == nullptr) continue;
        if (g_orig_gate(managed, g_gate_info)) ++open;
    }

    LOGI("23.1.3-content: the ExpOpenSystem table opens %zu/%zu curated "
         "content features on its own (gate queries=%" PRIu64
         ", opened locally=%" PRIu64 ")",
         open, kContentFeatureCount, g_queries, g_opened);
}

inline void pump() {
    if (!g_installed) return;
    ++g_frames;
    if (g_frames < kWarmupFrames) return;

    if (!g_reported) {
        g_reported = true;
        report_stock_state();
        return;
    }

    if ((g_frames % kReportPeriodFrames) == 0u) {
        LOGI("23.1.3-content: gate queries=%" PRIu64 " stock-open=%" PRIu64
             " opened locally=%" PRIu64 " distinct ids opened=%zu",
             g_queries, g_stock_open, g_opened, g_logged_count);
    }
}

// ------------------------------------------------------------ installation

inline bool install(uintptr_t il2cpp_base) {
    if (g_installed) return true;

    if (il2cpp_base == 0u) {
        LOGE("23.1.3-content: libil2cpp.so base address is unknown; the "
             "content gate cannot be armed");
        return false;
    }

    void* info = il2cpp::find_method_info(kGlobalNs, kGateClass, kGateOpen, 1);
    if (info == nullptr) {
        LOGE("23.1.3-content: the feature predicate %s::%s/1 is not in "
             "metadata; nothing was armed", kGateClass, kGateOpen);
        return false;
    }
    void* ptr = il2cpp::method_pointer(info);
    if (ptr == nullptr) {
        LOGE("23.1.3-content: the feature predicate has no compiled body; "
             "nothing was armed");
        return false;
    }

    // Four one-argument overloads share this metadata name. Only the string
    // overload may be hooked, so the resolved pointer must be exactly
    // base + its recorded RVA on the verified image.
    const auto expected =
        reinterpret_cast<void*>(il2cpp_base + kGateStringRva);
    if (ptr != expected) {
        LOGE("23.1.3-content: the resolved feature predicate is at %p but the "
             "verified string overload (RVA 0x%08" PRIxPTR ") maps to %p; "
             "refusing to patch an ambiguous overload",
             ptr, kGateStringRva, expected);
        return false;
    }

    g_gate_info = info;
    if (!hook::install({kGlobalNs, kGateClass, kGateOpen, 1},
                       reinterpret_cast<void*>(&gate_hook),
                       reinterpret_cast<void**>(&g_orig_gate), true)) {
        LOGE("23.1.3-content: the feature predicate could not be hooked; the "
             "lobby stays as the empty offline table describes it");
        return false;
    }

    g_installed = true;
    LOGI("23.1.3-content: armed: %zu content features (PixelPass battle pass, "
         "lotteries and card roulette, chests, task book and event content) "
         "report open when the offline ExpOpenSystem table has no entry for "
         "them", kContentFeatureCount);
    return true;
}

}  // namespace detail

// Arms the content gate. `il2cpp_base` is the load address of libil2cpp.so and
// is used to prove both the image and the overload before anything is patched.
inline bool install_hooks(uintptr_t il2cpp_base) {
    return detail::install(il2cpp_base);
}

// Driven once per main-menu frame from progression_2313's MainMenuController
// .Update hook: the one-shot stock-verdict report calls a managed predicate,
// so it needs a game thread and a live main menu.
inline void pump_from_main_menu() { detail::pump(); }

}  // namespace live_content_2313
