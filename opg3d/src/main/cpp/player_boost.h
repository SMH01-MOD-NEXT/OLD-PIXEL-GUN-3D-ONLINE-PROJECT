#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Persisted release progression grant for PG3D 13.2.1.
//
// The game deliberately consumes at most one level per AddExperience() call,
// regardless of how large the increment is. The grant therefore advances one
// level at a time until the stock level cap is reached. The ExpController UI
// listener is suppressed only while these synthetic level-up calls run, so no
// level-up panel/coroutine is created and the main menu remains responsive.
// Rewards, level calculation and persistence still run inside the original
// ExperienceController code.
//
// Currency is also written through the original BankController methods. This
// IL2CPP build uses the old ARM32 static-method ABI: r0 is a hidden null slot,
// while the first managed argument starts in r1. Omitting that slot makes
// AddCoins/AddGems receive count=0. The implementation below models that ABI
// explicitly and discovers the real currency keys by observing the getInt()
// call made by BankController itself; no guessed "Coins"/"Gems" keys are used.
//
// The lobby half of this module (namespace player_boost::lobby, below) grants
// the whole lobby craft catalogue the same way: through the client's own item
// grant and its own save, never by answering an ownership getter.
namespace player_boost {
namespace detail {

using MethodInfo = void;
using ManagedString = void;

// Instance methods use: self, managed args..., MethodInfo*.
using UpdateFn = void (*)(void* self, const MethodInfo* method);
using AddExperienceFn = void (*)(void* self, int32_t increment,
                                 const MethodInfo* method);
using ProgressUiFn = void (*)(void* self, void* progress,
                              const MethodInfo* method);

// Static methods in this metadata-v22 ARM32 build use a hidden null r0 before
// their managed arguments. The final MethodInfo* follows all managed args.
using GetLevelFn = int32_t (*)(void* static_context,
                               const MethodInfo* method);
using AddCurrencyFn = void (*)(void* static_context, int32_t count,
                               bool need_indication, int32_t accrual_type,
                               const MethodInfo* method);
using StoragerGetIntFn = int32_t (*)(void* static_context, ManagedString* key,
                                     bool suppress_warnings, bool direct_read,
                                     bool direct_read_v2,
                                     const MethodInfo* method);

inline UpdateFn g_menu_update = nullptr;
inline ProgressUiFn g_progress_ui = nullptr;
inline GetLevelFn g_get_level = nullptr;
inline const MethodInfo* g_mi_get_level = nullptr;
inline AddExperienceFn g_add_experience = nullptr;
inline const MethodInfo* g_mi_add_experience = nullptr;
inline AddCurrencyFn g_add_coins = nullptr;
inline const MethodInfo* g_mi_add_coins = nullptr;
inline AddCurrencyFn g_add_gems = nullptr;
inline const MethodInfo* g_mi_add_gems = nullptr;
inline StoragerGetIntFn g_storager_get_int = nullptr;
inline const MethodInfo* g_mi_get_int = nullptr;
inline void* g_shared_controller_field = nullptr;
inline void* g_max_exp_levels_field = nullptr;
inline void* g_health_by_level_field = nullptr;

inline constexpr int32_t kCurrencyTarget = 999999999;

// 38 is the last real player level in this release. The experience table is
// indexed from level 0, so its length is 39 and must never be used as a level
// target: asking for 39 makes the grant loop forever on a level that cannot
// be reached. Every computed cap is clamped to this value.
inline constexpr int32_t kLevelCap = 38;
inline constexpr int32_t kFallbackExpGrant = 9999999;
inline constexpr int32_t kMaxExpGrant = 0x3FFFFFFF;
inline constexpr uint32_t kLevelIntervalFrames = 5;
inline constexpr uint32_t kCurrencyIntervalFrames = 120;

struct CurrencyState {
    const char* label;
    ManagedString* key = nullptr;
    bool discovery_failed = false;
    bool unchanged_logged = false;
};

inline CurrencyState g_coins{"coins"};
inline CurrencyState g_gems{"gems"};
inline uint32_t g_frames = 0;
inline bool g_level_done = false;
inline int32_t g_last_level = -1;
inline uint32_t g_level_stalls = 0;
inline bool g_ui_suppression_logged = false;

// Currency discovery and AddExperience callbacks are synchronous on the main
// thread. thread_local keeps unrelated Storager calls on worker threads from
// being mistaken for our probe.
inline thread_local CurrencyState* g_capture_currency = nullptr;
inline thread_local bool g_suppress_level_ui = false;

void* read_static_field(void* field) {
    if (field == nullptr || il2cpp::field_static_get_value == nullptr) {
        return nullptr;
    }
    void* value = nullptr;
    il2cpp::field_static_get_value(field, &value);
    return value;
}

int32_t read_array_length(void* array) {
    if (array == nullptr) return -1;
    int32_t length = -1;
    std::memcpy(&length, static_cast<const char*>(array) + 0xC,
                sizeof(length));
    return length;
}

// MaxExpLevelsDefault contains entries for levels 0..38, so a length of 39
// still means the final valid player level is 38. HealthByLevel is used as a
// safety cross-check because gameplay indexes it with the current level, and
// the result is finally clamped to kLevelCap.
bool exp_table_info(int32_t* out_cap, int32_t* out_grant) {
    void* exp_array = read_static_field(g_max_exp_levels_field);
    const int32_t exp_length = read_array_length(exp_array);
    const int32_t health_length =
        read_array_length(read_static_field(g_health_by_level_field));
    if (exp_length <= 1 || exp_length > 4096) return false;

    int32_t safe_length = exp_length;
    if (health_length > 1 && health_length < safe_length) {
        safe_length = health_length;
    }
    int32_t cap = safe_length - 1;
    if (cap > kLevelCap) cap = kLevelCap;
    *out_cap = cap;

    int64_t total = 0;
    const char* elements = static_cast<const char*>(exp_array) + 0x10;
    for (int32_t i = 0; i < exp_length; ++i) {
        int32_t threshold = 0;
        std::memcpy(&threshold, elements + static_cast<size_t>(i) * 4u,
                    sizeof(threshold));
        if (threshold > 0) total += threshold;
    }
    if (total <= 0) return false;
    *out_grant = total > kMaxExpGrant ? kMaxExpGrant
                                      : static_cast<int32_t>(total);
    return true;
}

int32_t hook_storager_get_int(void* static_context, ManagedString* key,
                              bool suppress_warnings, bool direct_read,
                              bool direct_read_v2,
                              const MethodInfo* method) {
    const int32_t value = g_storager_get_int(
        static_context, key, suppress_warnings, direct_read, direct_read_v2,
        method);
    if (g_capture_currency != nullptr &&
        g_capture_currency->key == nullptr && key != nullptr) {
        // BankController reads its canonical balance key before invoking
        // analytics; keep the first read so nested analytics reads cannot
        // overwrite the discovered currency key.
        g_capture_currency->key = key;
    }
    return value;
}

void hook_progress_ui(void* self, void* progress, const MethodInfo* method) {
    if (g_suppress_level_ui) {
        if (!g_ui_suppression_logged) {
            g_ui_suppression_logged = true;
            LOGI("boost: automatic level-up UI suppression active");
        }
        return;
    }
    g_progress_ui(self, progress, method);
}

int32_t current_level() {
    return g_get_level(nullptr, g_mi_get_level);
}

void grant_level() {
    if (g_level_done) return;

    int32_t cap = kLevelCap;
    int32_t grant = kFallbackExpGrant;
    const bool have_table = exp_table_info(&cap, &grant);
    if (cap > kLevelCap) cap = kLevelCap;
    const int32_t before = current_level();
    if (before >= cap) {
        g_level_done = true;
        LOGI("boost: player level complete (%d/%d)", before, cap);
        return;
    }

    void* controller = read_static_field(g_shared_controller_field);
    if (controller == nullptr) return;

    // ExperienceController performs the actual save, level reward and state
    // transition. Its ExpController subscriber is UI-only; suppressing that
    // subscriber prevents the modal/coroutine without skipping persistence.
    g_suppress_level_ui = true;
    g_add_experience(controller, grant, g_mi_add_experience);
    g_suppress_level_ui = false;

    const int32_t after = current_level();
    if (after > before) {
        g_level_stalls = 0;
        if (after == cap || after % 5 == 0 || g_last_level < 0) {
            LOGI("boost: player level %d -> %d (target %d, +%d exp per stock "
                 "one-level step, table %s)",
                 before, after, cap, grant, have_table ? "read" : "fallback");
        }
        g_last_level = after;
        if (after >= cap) {
            g_level_done = true;
            LOGI("boost: final player level reached and saved (%d)", after);
        }
    } else {
        ++g_level_stalls;
        if (g_level_stalls == 1 || g_level_stalls % 60 == 0) {
            LOGW("boost: AddExperience did not advance level %d; retrying "
                 "without an attempt limit (stall=%u)",
                 before, g_level_stalls);
        }
    }
}

bool discover_currency_key(CurrencyState* state, AddCurrencyFn add,
                           const MethodInfo* add_mi) {
    if (state->key != nullptr) return true;
    if (state->discovery_failed) return false;

    // AddCoins/AddGems(0) still executes their canonical Storager.getInt ->
    // setInt path. The getInt hook captures the exact static key used by this
    // build without changing the balance.
    g_capture_currency = state;
    add(nullptr, 0, false, 0, add_mi);
    g_capture_currency = nullptr;

    if (state->key == nullptr) {
        state->discovery_failed = true;
        LOGE("boost: %s key discovery failed; currency grant disabled",
             state->label);
        return false;
    }
    const std::string key_name = il2cpp::to_utf8(state->key, 80);
    LOGI("boost: discovered canonical %s key '%s' through BankController",
         state->label, key_name.c_str());
    return true;
}

int32_t read_currency(const CurrencyState& state) {
    return g_storager_get_int(nullptr, state.key, false, false, false,
                              g_mi_get_int);
}

void grant_currency(CurrencyState* state, AddCurrencyFn add,
                    const MethodInfo* add_mi) {
    if (!discover_currency_key(state, add, add_mi)) return;

    const int32_t before = read_currency(*state);
    if (before >= kCurrencyTarget) return;

    int64_t delta64 = static_cast<int64_t>(kCurrencyTarget) - before;
    if (delta64 <= 0) return;
    if (delta64 > INT32_MAX) delta64 = INT32_MAX;
    const int32_t delta = static_cast<int32_t>(delta64);

    // Correct old-IL2CPP static ABI: hidden null r0, count in r1, indication
    // in r2, accrual type in r3, MethodInfo on the stack.
    add(nullptr, delta, false, 0, add_mi);
    const int32_t after = read_currency(*state);

    if (after <= before) {
        if (!state->unchanged_logged) {
            state->unchanged_logged = true;
            LOGE("boost: %s did not move after BankController(+%d): %d -> %d",
                 state->label, delta, before, after);
        }
        return;
    }
    state->unchanged_logged = false;
    LOGI("boost: %s %d -> %d via BankController (target %d)", state->label,
         before, after, kCurrencyTarget);
}

void maybe_grant() {
    ++g_frames;
    if (!g_level_done && g_frames % kLevelIntervalFrames == 1) {
        grant_level();
    }
    if (g_frames % kCurrencyIntervalFrames == 1) {
        grant_currency(&g_coins, g_add_coins, g_mi_add_coins);
        grant_currency(&g_gems, g_add_gems, g_mi_add_gems);
    }
}

void hook_menu_update(void* self, const MethodInfo* method) {
    g_menu_update(self, method);
    maybe_grant();
}

template <typename Fn>
void* replacement(Fn fn) {
    return reinterpret_cast<void*>(fn);
}

template <typename Fn>
void** original_slot(Fn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool resolve_call(const hook::ManagedMethod& target, void** out_fn,
                  const MethodInfo** out_mi) {
    void* info = il2cpp::find_method_info(target.namespaze, target.klass,
                                          target.method, target.args_count);
    void* pointer = il2cpp::method_pointer(info);
    if (info == nullptr || pointer == nullptr) {
        LOGE("boost: cannot resolve %s.%s/%d", target.klass, target.method,
             target.args_count);
        return false;
    }
    *out_fn = pointer;
    *out_mi = info;
    return true;
}

} // namespace detail

// ---------------------------------------------------------------------------
// The whole lobby craft catalogue, granted to the account.
//
// Lobby items are a progression of their own, unrelated to weapons and armour:
// bases, gates, fences, terrain, roads, small and big decor, static and
// dynamic backgrounds, effects, devices, the pet kennel and the paid bundles
// (LobbyItemGroupType 100..600, LobbyItemInfo.LobbyItemSlot Base..skybox).
// They are crafted in the lobby for coins/gems/real money, or gated behind
// in-match lockers (kill N mobs, win duels, kill with a gadget or a pet,
// collect lobby likes) that a private server can no longer make meaningful.
//
// The build ships the entire catalogue: LobbyItemsInfo.info describes every
// item that exists, and LobbyItemsController._allItems holds one LobbyItem per
// entry. Ownership is not a flag on that description — it is a separate
// per-item LobbyItemPlayerInfo record, and the client grants one
// unconditionally:
//
//   LobbyItemsController.AddItemNow(LobbyItem)          RVA 0x13E8534
//     +0x7C  bl  LobbyItem.get_IsExists()               ; refuses duplicates
//            bl  Object..ctor + LobbyItem.set_PlayerInfo(new player info)
//            bl  LobbyItemInfo.get_Id()                 ; InfoId of the record
//            bl  FriendsController.get_ServerTime()     ; craft marked done
//            bl  LobbyItem.get_CraftTime()
//     +0x9D0 bl  LobbyItemsController.Equip(item, silent)   ; bundle path only
//     +0x9D8 bl  LobbyItemsController.SavePlayerCurrentData()
//
//   SavePlayerCurrentData()                             RVA 0x13E81E0
//     +0x2D4 b   SaveLobbyItemsPlayerData(serialized)   RVA 0x13E0BB4
//              bl JsonUtility.ToJson(obj)               RVA 0x1AB7278
//              b  Storager.setString("lobby_items", json)  RVA 0xEBD760
//
// A scan of every branch in AddItemNow finds no BankController call, no
// ItemPrice read and no purchase path at all: it is the client's own grant.
// The game already uses it exactly the way this module does, for the starter
// items:
//
//   LobbyItemsController.GetFreeItemsIfNotExists()      RVA 0x13EC858
//     +0x168 bl  LobbyItem.get_IsExists()
//     +0x17C bl  LobbyItemsController.AddItemNow(item)
//
// So nothing here is an ownership getter override. The account really receives
// every item as a real LobbyItemPlayerInfo record, written by the game's own
// save under the "lobby_items" Storager key, and it survives a restart. Prices,
// craft timers, lockers, item effects/buffs and the cloud merge
// (LobbyItemsCloudApplyer) are left exactly as they are.
namespace lobby {

using MethodInfo = void;
using ManagedString = void;

using UpdateFn = void (*)(void* self, const MethodInfo* method);
using BoolInstanceFn = bool (*)(void* self, const MethodInfo* method);
using ObjectInstanceFn = void* (*)(void* self, const MethodInfo* method);
using VoidInstanceFn = void (*)(void* self, const MethodInfo* method);
using AddItemFn = bool (*)(void* self, void* item, const MethodInfo* method);
using ListCountFn = int32_t (*)(void* self, const MethodInfo* method);
using ListItemFn = void* (*)(void* self, int32_t index,
                             const MethodInfo* method);

inline UpdateFn g_controller_update = nullptr;

inline BoolInstanceFn g_is_ready = nullptr;
inline const MethodInfo* g_mi_is_ready = nullptr;
inline ObjectInstanceFn g_all_items = nullptr;
inline const MethodInfo* g_mi_all_items = nullptr;
inline AddItemFn g_add_item_now = nullptr;
inline const MethodInfo* g_mi_add_item_now = nullptr;
inline VoidInstanceFn g_save_player_data = nullptr;
inline const MethodInfo* g_mi_save_player_data = nullptr;
inline BoolInstanceFn g_item_exists = nullptr;
inline const MethodInfo* g_mi_item_exists = nullptr;
inline ObjectInstanceFn g_item_id = nullptr;
inline const MethodInfo* g_mi_item_id = nullptr;

// List<LobbyItem> is a generic instantiation, so its accessors are taken from
// the class of the live list instead of being looked up by name in metadata.
inline ListCountFn g_list_count = nullptr;
inline const MethodInfo* g_mi_list_count = nullptr;
inline ListItemFn g_list_item = nullptr;
inline const MethodInfo* g_mi_list_item = nullptr;
inline bool g_list_api_ready = false;
inline bool g_list_api_failed = false;

// Every grant writes the whole lobby save (JsonUtility.ToJson +
// Storager.setString), so the catalogue is walked in small batches instead of
// in one frame. All of this runs on the main thread inside
// LobbyItemsController.Update, so plain statics are sufficient.
inline constexpr int32_t kGrantsPerTick = 3;
inline constexpr int32_t kScannedPerTick = 96;
inline constexpr uint32_t kMaxLoggedGrants = 40;
inline constexpr uint32_t kMaxFailures = 32;
inline constexpr int32_t kMaxPasses = 8;
inline constexpr uint32_t kRecheckTicks = 1800;

inline bool g_disabled = false;
inline bool g_complete = false;
inline int32_t g_cursor = 0;
inline int32_t g_pass = 0;
inline uint32_t g_pass_granted = 0;
inline uint32_t g_granted_total = 0;
inline uint32_t g_failures = 0;
inline uint32_t g_logged_grants = 0;
inline uint32_t g_logged_failures = 0;
inline uint32_t g_idle_ticks = 0;

std::string item_name(void* item) {
    if (item == nullptr || g_item_id == nullptr) {
        return std::string("<id-api-unavailable>");
    }
    void* id = g_item_id(item, g_mi_item_id);
    if (id == nullptr) return std::string("<null>");
    return il2cpp::to_utf8(id, 64);
}

bool resolve_list_api(void* list) {
    if (g_list_api_ready) return true;
    if (g_list_api_failed) return false;
    if (list == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) {
        return false;
    }

    void* klass = il2cpp::object_get_class(list);
    if (klass == nullptr) {
        g_list_api_failed = true;
        LOGE("boost: the lobby catalogue list has no class; lobby items are "
             "not granted");
        return false;
    }

    void* count_info = il2cpp::class_get_method_from_name(klass, "get_Count", 0);
    void* item_info = il2cpp::class_get_method_from_name(klass, "get_Item", 1);
    void* count_ptr = il2cpp::method_pointer(count_info);
    void* item_ptr = il2cpp::method_pointer(item_info);
    if (count_ptr == nullptr || item_ptr == nullptr) {
        g_list_api_failed = true;
        LOGE("boost: List<LobbyItem> get_Count/get_Item could not be resolved "
             "(%d/%d); lobby items are not granted",
             count_ptr != nullptr ? 1 : 0, item_ptr != nullptr ? 1 : 0);
        return false;
    }

    g_list_count = reinterpret_cast<ListCountFn>(count_ptr);
    g_mi_list_count = count_info;
    g_list_item = reinterpret_cast<ListItemFn>(item_ptr);
    g_mi_list_item = item_info;
    g_list_api_ready = true;
    return true;
}

void finish_pass(int32_t count) {
    ++g_pass;
    if (g_pass_granted > 0 && g_pass < kMaxPasses) {
        LOGI("boost: lobby pass %d granted %u item(s) out of %d in the "
             "catalogue; verifying once more",
             g_pass, g_pass_granted, count);
        g_cursor = 0;
        g_pass_granted = 0;
        return;
    }
    g_complete = true;
    g_cursor = 0;
    g_pass_granted = 0;
    g_idle_ticks = 0;
    LOGI("boost: lobby catalogue complete — %d item(s) exist in this build, "
         "%u granted and saved by this module (Storager key 'lobby_items')",
         count, g_granted_total);
}

void grant_batch(void* self) {
    void* items = g_all_items(self, g_mi_all_items);
    if (items == nullptr || !resolve_list_api(items)) return;

    const int32_t count = g_list_count(items, g_mi_list_count);
    if (count <= 0) return;
    if (g_cursor >= count) {
        finish_pass(count);
        return;
    }

    int32_t scanned = 0;
    int32_t granted = 0;
    while (g_cursor < count && scanned < kScannedPerTick &&
           granted < kGrantsPerTick) {
        void* item = g_list_item(items, g_cursor, g_mi_list_item);
        ++g_cursor;
        ++scanned;
        if (item == nullptr) continue;
        // Ownership is read, never answered: this is the same question the
        // stock free-item loop asks before granting.
        if (g_item_exists(item, g_mi_item_exists)) continue;

        const std::string id = item_name(item);
        if (!g_add_item_now(self, item, g_mi_add_item_now)) {
            ++g_failures;
            if (g_logged_failures < kMaxLoggedGrants) {
                ++g_logged_failures;
                LOGW("boost: the client refused to grant lobby item '%s'; it "
                     "stays unowned",
                     id.c_str());
            }
            if (g_failures >= kMaxFailures) {
                g_disabled = true;
                LOGE("boost: %u lobby grants were refused in a row; the lobby "
                     "catalogue grant is disabled to leave the save alone",
                     g_failures);
                return;
            }
            continue;
        }

        ++granted;
        ++g_pass_granted;
        ++g_granted_total;
        if (g_logged_grants < kMaxLoggedGrants) {
            ++g_logged_grants;
            LOGI("boost: lobby item '%s' granted through AddItemNow and saved "
                 "(%u so far)",
                 id.c_str(), g_granted_total);
        }
    }

    // AddItemNow saves on its own; this is the game's own save method called
    // once per batch so a partially walked catalogue is still persisted.
    if (granted > 0 && g_save_player_data != nullptr) {
        g_save_player_data(self, g_mi_save_player_data);
    }
    if (g_cursor >= count) finish_pass(count);
}

void hook_controller_update(void* self, const MethodInfo* method) {
    g_controller_update(self, method);
    if (self == nullptr || g_disabled) return;
    // Items are constructed by InitItems()/ReadPlayerData() first; granting
    // before the controller reports itself ready would race that setup.
    if (!g_is_ready(self, g_mi_is_ready)) return;

    if (g_complete) {
        // A cloud merge or a re-read can reintroduce missing records, so the
        // catalogue is re-verified occasionally instead of once per process.
        if (++g_idle_ticks < kRecheckTicks) return;
        g_idle_ticks = 0;
        g_complete = false;
        g_cursor = 0;
        g_pass = 0;
        g_pass_granted = 0;
    }
    grant_batch(self);
}

inline bool install() {
    bool ok = detail::resolve_call(
        {"Rilisoft", "LobbyItemsController", "get_IsReady", 0},
        reinterpret_cast<void**>(&g_is_ready), &g_mi_is_ready);
    ok &= detail::resolve_call(
        {"Rilisoft", "LobbyItemsController", "get_AllItems", 0},
        reinterpret_cast<void**>(&g_all_items), &g_mi_all_items);
    ok &= detail::resolve_call(
        {"Rilisoft", "LobbyItemsController", "AddItemNow", 1},
        reinterpret_cast<void**>(&g_add_item_now), &g_mi_add_item_now);
    ok &= detail::resolve_call({"Rilisoft", "LobbyItem", "get_IsExists", 0},
                               reinterpret_cast<void**>(&g_item_exists),
                               &g_mi_item_exists);
    if (!ok) {
        LOGE("boost: the lobby catalogue API could not be resolved; lobby "
             "craft items are NOT granted");
        return false;
    }

    // Optional: an extra save per batch, and item ids for the log lines.
    if (!detail::resolve_call(
            {"Rilisoft", "LobbyItemsController", "SavePlayerCurrentData", 0},
            reinterpret_cast<void**>(&g_save_player_data),
            &g_mi_save_player_data)) {
        g_save_player_data = nullptr;
        LOGW("boost: SavePlayerCurrentData is unavailable; lobby grants rely "
             "on the save AddItemNow performs itself");
    }
    if (!detail::resolve_call({"Rilisoft", "LobbyItem", "get_Id", 0},
                              reinterpret_cast<void**>(&g_item_id),
                              &g_mi_item_id)) {
        g_item_id = nullptr;
        LOGW("boost: LobbyItem.get_Id is unavailable; lobby grants will not be "
             "named in the log");
    }

    if (!hook::install({"Rilisoft", "LobbyItemsController", "Update", 0},
                       detail::replacement(&hook_controller_update),
                       detail::original_slot(&g_controller_update), false)) {
        LOGE("boost: LobbyItemsController.Update could not be hooked; lobby "
             "craft items are NOT granted");
        return false;
    }

    LOGI("boost: lobby craft grant armed (source=LobbyItemsController.AllItems, "
         "grant=AddItemNow + SavePlayerCurrentData, batch=%d per frame, "
         "ownership getters=untouched, prices/lockers/effects=stock)",
         kGrantsPerTick);
    return true;
}

} // namespace lobby

inline bool install_hooks() {
    bool ok = true;
    ok &= detail::resolve_call({"", "ExperienceController", "GetCurrentLevel", 0},
                               reinterpret_cast<void**>(&detail::g_get_level),
                               &detail::g_mi_get_level);
    ok &= detail::resolve_call({"", "ExperienceController", "AddExperience", 1},
                               reinterpret_cast<void**>(&detail::g_add_experience),
                               &detail::g_mi_add_experience);
    ok &= detail::resolve_call({"", "BankController", "AddCoins", 3},
                               reinterpret_cast<void**>(&detail::g_add_coins),
                               &detail::g_mi_add_coins);
    ok &= detail::resolve_call({"", "BankController", "AddGems", 3},
                               reinterpret_cast<void**>(&detail::g_add_gems),
                               &detail::g_mi_add_gems);
    ok &= detail::resolve_call({"", "Storager", "getInt", 4},
                               reinterpret_cast<void**>(&detail::g_storager_get_int),
                               &detail::g_mi_get_int);

    detail::g_shared_controller_field =
        il2cpp::find_field("", "ExperienceController", "sharedController");
    detail::g_max_exp_levels_field =
        il2cpp::find_field("", "ExperienceController", "MaxExpLevelsDefault");
    detail::g_health_by_level_field =
        il2cpp::find_field("", "ExperienceController", "HealthByLevel");
    if (detail::g_shared_controller_field == nullptr) {
        LOGE("boost: ExperienceController.sharedController field not found");
        ok = false;
    }
    if (detail::g_max_exp_levels_field == nullptr) {
        LOGW("boost: MaxExpLevelsDefault not found; level cap will use %d",
             detail::kLevelCap);
    }
    if (!ok) {
        LOGE("boost: progression targets incomplete; grant disabled");
        return false;
    }

    const bool currency_capture = hook::install(
        {"", "Storager", "getInt", 4},
        detail::replacement(&detail::hook_storager_get_int),
        detail::original_slot(&detail::g_storager_get_int), true);
    const bool level_ui = hook::install(
        {"", "ExpController", "ExperienceControllerOnPlayerProgressChanged", 1},
        detail::replacement(&detail::hook_progress_ui),
        detail::original_slot(&detail::g_progress_ui), true);
    if (!currency_capture || !level_ui) {
        LOGE("boost: currency-key capture or level-up UI suppression hook failed");
        return false;
    }

    static const hook::ManagedMethod kTriggers[] = {
        {"", "MainMenuController", "Update", 0},
        {"", "BankController", "Update", 0},
        {"", "ExpController", "Update", 0},
    };
    const char* chosen = nullptr;
    for (const auto& trigger : kTriggers) {
        if (hook::install(trigger, detail::replacement(&detail::hook_menu_update),
                          detail::original_slot(&detail::g_menu_update), false)) {
            chosen = trigger.klass;
            break;
        }
    }
    if (chosen == nullptr) {
        LOGE("boost: no menu Update hook target available; grant disabled");
        return false;
    }

    LOGI("boost: persisted grant armed (trigger=%s.Update, level target=%d, "
         "currency target=%d, level-up UI=skipped)",
         chosen, detail::kLevelCap, detail::kCurrencyTarget);

    // The lobby catalogue is a separate progression and a separate save file.
    // Its failure must not take the level and currency grants down with it.
    if (!lobby::install()) {
        LOGW("boost: lobby craft items are not being granted; the level and "
             "currency grants above are unaffected");
    }
    return true;
}

} // namespace player_boost
