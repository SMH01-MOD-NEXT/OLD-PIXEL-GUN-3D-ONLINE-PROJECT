#pragma once

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Repairs the rank number presentation without changing progression values.
// Device evidence and the 23.1.3 call graph both show that the canonical level
// getter already returns 65. RankIndicatorGuiElement also writes that value to
// _rankLable before it formats experience, so a missing number is a disabled,
// inactive, transparent, or stale UILabel rather than a second level store.
//
// Both rank widgets used by this build are covered:
//   Rilisoft.RankIndicatorGuiElement._rankLable (mode-selection UI)
//   PlayerPanel.rankLabel                         (legacy/main panel)
// After each stock refresh the label is activated, enabled, made opaque and
// assigned the canonical ExperienceController level text.
namespace rank_ui_2313 {
namespace detail {

using MethodInfo = void;
using RefreshFn = void (*)(void*, bool, const MethodInfo*);
using InstanceVoidFn = void (*)(void*, const MethodInfo*);
using StaticIntFn = int32_t (*)(const MethodInfo*);
using GetTextFn = void* (*)(void*, const MethodInfo*);
using SetTextFn = void (*)(void*, void*, const MethodInfo*);
using GetGameObjectFn = void* (*)(void*, const MethodInfo*);
using SetActiveFn = void (*)(void*, bool, const MethodInfo*);
using SetEnabledFn = void (*)(void*, bool, const MethodInfo*);
using SetAlphaFn = void (*)(void*, float, const MethodInfo*);

inline constexpr int32_t kMinRank = 1;
inline constexpr int32_t kMaxRank = 65;
inline constexpr const char* kRankRefresh =
    u8"\u4E0E\u4E1B\u4E17\u4E09\u4E0A\u4E05\u4E13\u4E04\u4E1D";
inline constexpr const char* kLevelGetter =
    u8"\u4E16\u4E10\u4E19\u4E06\u4E1A\u4E00\u4E04\u4E19\u4E12";

struct ManagedCall {
    const MethodInfo* info = nullptr;
    void* pointer = nullptr;
    explicit operator bool() const noexcept {
        return info != nullptr && pointer != nullptr;
    }
};

inline RefreshFn g_rank_refresh = nullptr;
inline InstanceVoidFn g_player_update_exp = nullptr;
inline ManagedCall g_level{};
inline ManagedCall g_get_text{};
inline ManagedCall g_set_text{};
inline ManagedCall g_get_game_object{};
inline ManagedCall g_set_active{};
inline ManagedCall g_set_enabled{};
inline ManagedCall g_set_alpha{};
inline void* g_rank_indicator_field = nullptr;
inline void* g_player_panel_field = nullptr;
inline std::atomic<uint32_t> g_repairs{0u};
inline std::atomic<uint32_t> g_missing_labels{0u};

bool bind(ManagedCall& out, const char* namespaze, const char* klass,
          const char* method, int args_count, bool required) {
    void* info = il2cpp::find_method_info(namespaze, klass, method, args_count);
    void* pointer = info != nullptr ? il2cpp::method_pointer(info) : nullptr;
    out.info = reinterpret_cast<const MethodInfo*>(info);
    out.pointer = pointer;
    if (out) return true;
    if (required) {
        LOGE("23.1.3-rank-ui: cannot resolve %s.%s/%d", klass, method,
             args_count);
    } else {
        LOGW("23.1.3-rank-ui: optional %s.%s/%d is unavailable", klass,
             method, args_count);
    }
    return false;
}

int32_t canonical_level() {
    if (!g_level) return 0;
    return reinterpret_cast<StaticIntFn>(g_level.pointer)(g_level.info);
}

void* read_label(void* owner, void* field) {
    if (owner == nullptr || field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return nullptr;
    }
    void* label = nullptr;
    il2cpp::field_get_value(owner, field, &label);
    return label;
}

void repair_label(void* owner, void* field, const char* source) {
    const int32_t level = canonical_level();
    if (level < kMinRank || level > kMaxRank) {
        LOGW("23.1.3-rank-ui: ignored invalid canonical level %" PRId32
             " from %s", level, source);
        return;
    }

    void* label = read_label(owner, field);
    if (label == nullptr) {
        const uint32_t missing = g_missing_labels.fetch_add(1u) + 1u;
        if (missing <= 8u) {
            LOGW("23.1.3-rank-ui: %s rank label is null (#%u)", source,
                 missing);
        }
        return;
    }

    std::string before = "<unavailable>";
    if (g_get_text) {
        void* old_text = reinterpret_cast<GetTextFn>(g_get_text.pointer)(
            label, g_get_text.info);
        before = il2cpp::to_utf8(old_text, 48u);
    }

    if (g_get_game_object && g_set_active) {
        void* game_object =
            reinterpret_cast<GetGameObjectFn>(g_get_game_object.pointer)(
                label, g_get_game_object.info);
        if (game_object != nullptr) {
            reinterpret_cast<SetActiveFn>(g_set_active.pointer)(
                game_object, true, g_set_active.info);
        }
    }
    if (g_set_enabled) {
        reinterpret_cast<SetEnabledFn>(g_set_enabled.pointer)(
            label, true, g_set_enabled.info);
    }
    if (g_set_alpha) {
        reinterpret_cast<SetAlphaFn>(g_set_alpha.pointer)(
            label, 1.0f, g_set_alpha.info);
    }

    const std::string value = std::to_string(level);
    void* managed = il2cpp::string_new(value.c_str());
    if (managed == nullptr) {
        LOGE("23.1.3-rank-ui: could not allocate rank text '%s'",
             value.c_str());
        return;
    }
    reinterpret_cast<SetTextFn>(g_set_text.pointer)(label, managed,
                                                    g_set_text.info);

    const uint32_t repair = g_repairs.fetch_add(1u) + 1u;
    if (repair <= 8u || repair % 120u == 0u) {
        LOGI("23.1.3-rank-ui: %s rank label repaired #%u '%s' -> '%s' "
             "(active=1 enabled=1 alpha=1)",
             source, repair, before.c_str(), value.c_str());
    }
}

void rank_refresh_hook(void* self, bool animate, const MethodInfo* method) {
    if (g_rank_refresh == nullptr) {
        LOGE("23.1.3-rank-ui: RankIndicator refresh has no saved original");
        return;
    }
    g_rank_refresh(self, animate, method);
    repair_label(self, g_rank_indicator_field, "RankIndicatorGuiElement");
}

void player_update_exp_hook(void* self, const MethodInfo* method) {
    if (g_player_update_exp == nullptr) {
        LOGE("23.1.3-rank-ui: PlayerPanel.UpdateExp has no saved original");
        return;
    }
    g_player_update_exp(self, method);
    repair_label(self, g_player_panel_field, "PlayerPanel");
}

} // namespace detail

inline bool install_hooks() {
#if defined(__ANDROID__)
    static_assert(sizeof(void*) == 8,
                  "PG3D 23.1.3 target must be arm64-v8a");
#endif
    using namespace detail;

    bool api = true;
    api &= bind(g_level, "", "ExperienceController", kLevelGetter, 0, true);
    bind(g_get_text, "", "UILabel", "get_text", 0, false);
    api &= bind(g_set_text, "", "UILabel", "set_text", 1, true);
    bind(g_get_game_object, "UnityEngine", "Component", "get_gameObject", 0,
         false);
    bind(g_set_active, "UnityEngine", "GameObject", "SetActive", 1, false);
    bind(g_set_enabled, "UnityEngine", "Behaviour", "set_enabled", 1, false);
    bind(g_set_alpha, "", "UIWidget", "set_alpha", 1, false);

    g_rank_indicator_field = il2cpp::find_field(
        "Rilisoft", "RankIndicatorGuiElement", "_rankLable");
    g_player_panel_field =
        il2cpp::find_field("", "PlayerPanel", "rankLabel");
    const bool fields =
        g_rank_indicator_field != nullptr && g_player_panel_field != nullptr;
    if (!fields) {
        LOGE("23.1.3-rank-ui: rank fields missing (indicator=%d panel=%d)",
             g_rank_indicator_field != nullptr ? 1 : 0,
             g_player_panel_field != nullptr ? 1 : 0);
    }

    int installed = 0;
    const bool indicator = hook::install(
        {"Rilisoft", "RankIndicatorGuiElement", kRankRefresh, 1},
        reinterpret_cast<void*>(&rank_refresh_hook),
        reinterpret_cast<void**>(&g_rank_refresh), true);
    if (indicator) ++installed;
    const bool player_panel = hook::install(
        {"", "PlayerPanel", "UpdateExp", 0},
        reinterpret_cast<void*>(&player_update_exp_hook),
        reinterpret_cast<void**>(&g_player_update_exp), false);
    if (player_panel) ++installed;

    const bool ready = api && fields && indicator;
    LOGI("23.1.3-rank-ui: installed %d/2 refresh hooks "
         "(api=%d fields=%d indicator=%d player-panel=%d)",
         installed, api ? 1 : 0, fields ? 1 : 0, indicator ? 1 : 0,
         player_panel ? 1 : 0);
    if (!ready) {
        LOGE("23.1.3-rank-ui: rank presentation repair is incomplete");
    }
    return ready;
}

} // namespace rank_ui_2313
