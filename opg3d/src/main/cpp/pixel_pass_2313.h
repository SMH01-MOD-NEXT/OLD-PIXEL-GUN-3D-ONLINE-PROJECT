#pragma once

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"

// Offline PixelPass season for the exact supplied 23.1.3 ARM64 libil2cpp.so.
//
// What the device proved, in order
// -------------------------------
// 1. The UI is complete and the view runs. From a 33 s capture with the lobby
//    up:
//
//      PixelPassLobbyView.OnEnable ran (holder=1 lock=1 unlock=1
//        coming-soon=1 need-level=1 button=1 view service=null;
//        manager=alive manager service=null season=absent)
//      pass manager alive (service=null season=absent); lobby view OnEnable
//        ran 1 time(s); gates opened by this port A=0 B=0 C=0
//
//    Every state container and the lobby button itself are present, the
//    manager singleton is alive, and OnEnable does run. Exactly one object is
//    missing: the season. Because the pass service takes the season in its
//    constructor, a null season means a null service, which is what makes the
//    view switch its whole _holder off -- no button, not even a coming-soon
//    state.
//
//    The gate override deliberately stayed off (A=0 B=0 C=0). With a null
//    season, opening a gate would have sent the view straight into
//    dereferencing it: a broken menu instead of a missing button. These gates
//    must never be forced without a season.
//
// 2. Overload ambiguity is real, not theoretical. The first attempt at
//    building the season was refused by its own guard:
//
//      JsonConvert::DeserializeObject/2 resolved to RVA 0x40bc7c4 but this
//        build expects 0x40bc82c; refusing it
//      the season construction path did not verify against this build
//
//    0x40BC7C4 is DeserializeObject(string, JsonSerializerSettings), so
//    il2cpp_class_get_method_from_name really does return whichever overload
//    metadata lists first. Passing a System.Type where a settings object
//    belongs would have been a type-confused call into managed code.
//    Everything else on the path verified silently in that same run: both
//    classes resolved, the service .ctor at 0x18F0594 and the manager setter
//    at 0x1A08114 matched their RVAs, and il2cpp_object_new,
//    il2cpp_class_get_type and il2cpp_type_get_object all bound.
//
// 3. The config cache is not the route, confirmed three sessions running:
//
//      the stock config cache asked the inner loader for config id 102
//      the stock config cache asked the primary loader for config id 102
//      ... cache reads=2 (config 123 reads=0)
//
//    ConfigId 102 is MessagePackTest. Over a whole session the cache is
//    consulted exactly twice, both times for a serialiser self-test, and never
//    once for PixelPass; the save path never fires. Those hooks are kept
//    because they cost nothing and produced this evidence, but they are not
//    the mechanism.
//
// How the season is built
// -----------------------
// PGCompany.PixelPass.丐丑业丒丈丅丐专丅 (TypeDefIndex 13225) is [JsonObject] and
// every field carries an explicit [JsonProperty] name:
//
//   "c"   Common               "p"   Pages          "l"   Levels
//   "t"   Tasks                "prt" _premiumTaskIndexes
//   "tb"  TasksBase            "at"  TasksForAds
//   "r"   GameRewards          "of"  Offers
//
// That is precisely the JSON this module authors, so the stock serialiser can
// build it. Native code here could not: the DTO graph is full of List<T>
// instantiations and converter-backed salted ints, and this port has no
// generic-instantiation helper.
//
// There are two independent routes, and every target on both is selected by
// RVA so neither can pick a sibling overload:
//
//   route 1   DeserializeObject(string, Type)            0x40BC82C
//             found by walking JsonConvert's method list for the entry point
//             at base + that RVA, which yields the correct pointer and the
//             MethodInfo that belongs to it, together.
//
//   route 2   il2cpp_object_new(season)
//               -> season .ctor()                        0x1A05768
//               -> PopulateObject(json, season)          0x40BCAB8
//             PopulateObject has exactly one two-argument overload, so name
//             plus arity already identifies it and the RVA check only
//             confirms the build. This route needs no method walk, so it
//             survives a runtime that does not export one.
//
// Route 1 runs first because it builds the graph in a single managed call. If
// it returns null rather than throwing, route 2 still runs: PopulateObject
// fills an already-allocated instance and does not have to construct the root,
// so it tolerates shapes the typed parse rejects.
//
// Then, in both cases:
//
//   il2cpp_object_new(pass service)
//     -> pass service .ctor(season)                      0x18F0594
//     -> manager 丝世东丛丗下丑丟丞(service)                    0x1A08114
//     -> PixelPassLobbyView 丗且丈丁丕丕丘一丞 (+0x110)
//
// Construction is driven from the manager's own gates. They are the earliest
// point at which the game asks about the pass -- on device they are answered
// ~3.6 s in, from AppsMenu.Start on the loading screen -- and a shut gate is
// precisely what removes the lobby entry. The gate hook builds the season, hands
// it over and then re-reads it, so the same call can already answer true.
//
// Waiting for the view instead deadlocks: gate A answers false, so the lobby
// never creates the pass entry, so PixelPassLobbyView.OnEnable never runs, so no
// season is ever built, so gate A keeps answering false. The OnEnable hook is
// kept as a second entry point because it also writes the view's cached service
// field, which the view may have read in Awake before this hook ran, and pump()
// retries on menu frames for the case where nothing was buildable yet.
//
// Also ruled out: BalanceController.世丄丅丏丌专上世丄(string, byte[], 丐丛丏丒丘东三专一,
// ConfigId) at 0x471CB40 would drive the game's own parse/validate/raise path,
// but BalanceController exposes no static instance accessor anywhere in its
// metadata, so the instance cannot be reached from native code.
//
// Remaining risk, stated plainly
// ------------------------------
// The season is validated by
// DataSystem.DataValidation.FluentValidators.丅丑世丈世七丈丂丁, an
// AbstractValidator<丐丑业丒丈丅丐专丅> (TypeDefIndex 7760, ctor 0x2D484D0), with
// sibling validators for the Common (7762) and Page (7764) DTOs. Those rules
// live in constructor bodies, which a metadata dump does not contain. A
// managed exception cannot be caught from native frames, so the construction
// is attempted exactly once -- the attempt flag is set *before* the first
// managed call, never after -- and every step logs immediately before and
// after itself, so a rejection names the step instead of going quiet.
//
// Two properties of the build make the hand-authored JSON safe:
//
//   * Rilisoft.丅丏丏丛丕丁丟上丞, the salted int used for SeasonId, tier Level,
//     NumPage, Exp and IsFree, is tagged
//     [JsonConverter(typeof(七不不丐专世丝丄上))] (TypeDefIndex 9203). In JSON it is
//     therefore a plain *number* and the converter re-salts it on read: no
//     salt is ever fabricated, so nothing looks tampered with to the client.
//     Note that IsFree ("f") is one of these salted ints and NOT a bool --
//     emitting `true` there cannot be read back.
//   * Rewards are strings. PGCompany.丏不丏丂丙丐专丏丅.ReadJson (0x33494A4) hands
//     the token to DataSystem.DataCollectors.丒丗丘万一七与丟丕.丌丄丛丈与丝丑世丆
//     (0x2B005A4), and 丑一丘与丁丄专专专.丅专万三丙业丗丟一 (0x24B4260) splits the string
//     on ':' (movz w1, #0x3A) and int-parses the first field, which matches
//     the (OfferItemType, string id, int amount) constructor at 0x24B39D8. A
//     reward is therefore "<type>:<id>:<amount>", e.g. "1170:<skin id>:1".
//
// Only ids the build itself reports are written for skins: they come from
// Rilisoft.与世且一丁丆丈丄丈.丛上丌丏丟丒东丂且(), which is backed by the local
// WeaponSkins resource and is therefore populated with no network at all.
//
// Everything is fail-closed: if a metadata target is missing, if an RVA does
// not match, if the skin catalogue is still empty, or if any managed step
// returns null, the module reports it and leaves the game exactly as it was.
namespace pixel_pass_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ----------------------------------------------------------- metadata names

constexpr const char* kPgNs = "PGCompany";
constexpr const char* kPassNs = "PGCompany.PixelPass";
constexpr const char* kRilisoftNs = "Rilisoft";
constexpr const char* kSystemNs = "System";
constexpr const char* kJsonNs = "Newtonsoft.Json";

constexpr const char* kStorageClass = "丅丝业七三丈丝丑丏";
constexpr const char* kStorageInstance = "下丌丑丁下丟丛丘上";

// The three read entry points of the stock cache, plus the write path. The
// device has shown the game only ever asks this class for ConfigId 102, so
// these are diagnostics and a safety net rather than the delivery route.
constexpr const char* kStorageLoad = "东丗与丏丟丛丂三丞";       // /3, RVA 0x249D670
constexpr const char* kStorageLoadAlt = "与丌下丑丝丁丄丏丛";    // /3, RVA 0x249E064
constexpr const char* kStorageLoadInner = "丅专东与上丆不丏丐";  // /4, RVA 0x249DACC
constexpr const char* kStorageSave = "与万丝丗丁不丗一丗";       // /3, RVA 0x249CD64

// The pass manager: this is what the lobby view reads.
constexpr const char* kManagerClass = "万丈丏丈丙丑万万丙";       // 13248
constexpr const char* kManagerInstance = "下丌丑丁下丟丛丘上";    // static /0, 0x1A07F4C
constexpr const char* kManagerSeason = "丒不丏一丂丈丙东丟";      // /0, 0x1A08038
constexpr const char* kManagerService = "上丄丟三丏三丒丄东";     // /0, 0x1A0810C
constexpr const char* kManagerSetService = "丝世东丛丗下丑丟丞";  // /1, 0x1A08114
constexpr const char* kManagerGateA = "丈丁上一丟丈丗七业";       // bool /0, 0x1A0811C
constexpr const char* kManagerGateB = "且丗丛不东万三业丄";       // bool /0, 0x1A0815C
constexpr const char* kManagerGateC = "专丒丂丂丕业丛丐丂";       // bool /0, 0x1A08460

// The season DTO and the pass service that wraps it.
constexpr const char* kSeasonClass = "丐丑业丒丈丅丐专丅";        // 13225
constexpr const char* kServiceClass = "三丄三丂丈七业丁丞";       // 13268
constexpr const char* kCtor = ".ctor";

// The lobby view itself. Its serialised field names are not obfuscated.
constexpr const char* kLobbyViewClass = "PixelPassLobbyView";  // 12069
constexpr const char* kLobbyViewOnEnable = "OnEnable";         // /0, 0x28F4C68
constexpr const char* kViewHolder = "_holder";                 // 0x48
constexpr const char* kViewLock = "_lockContainer";            // 0x70
constexpr const char* kViewUnlock = "_unLockContainer";        // 0x78
constexpr const char* kViewComingSoon = "_comingSoonContainer";  // 0x80
constexpr const char* kViewNeedLevel = "_needLevelContainer";  // 0x88
constexpr const char* kViewLobbyButton = "_lobbyButton";       // 0xE0
constexpr const char* kViewService = "丗且丈丁丕丕丘一丞";         // 0x110

constexpr const char* kConvertClass = "Convert";
constexpr const char* kFromBase64 = "FromBase64String";

constexpr const char* kJsonConvertClass = "JsonConvert";       // 18246
constexpr const char* kPopulateObject = "PopulateObject";      // /2, 0x40BCAB8

constexpr const char* kSkinCatalogueClass = "与世且一丁丆丈丄丈";
constexpr const char* kSkinIdList = "丛上丌丏丟丒东丂且";

// ------------------------------------------------------------------- RVAs
//
// Every managed target this module *calls* (as opposed to hooks) is verified
// against these before use. Two reasons: methods that share a name and an
// argument count cannot be told apart by metadata lookup, and a different
// libil2cpp.so must disarm the season path instead of jumping to an arbitrary
// address.
//
// DeserializeObject is the case that forced this. Its two-argument overloads
// are (string, JsonSerializerSettings) at 0x40BC7C4 and (string, Type) at
// 0x40BC82C, and the device showed metadata lookup returning the former. It is
// therefore bound by walking the method list for the entry point at base plus
// its RVA, not by name.
//
// PopulateObject needs no walk: (string, object) at 0x40BCAB8 is the only
// two-argument overload, the other being (string, object,
// JsonSerializerSettings) at 0x40BCB20.
constexpr uintptr_t kRvaDeserializeStringType = 0x40BC82Cu;
constexpr uintptr_t kRvaPopulateObject = 0x40BCAB8u;
constexpr uintptr_t kRvaSeasonCtor = 0x1A05768u;
constexpr uintptr_t kRvaServiceCtor = 0x18F0594u;
constexpr uintptr_t kRvaManagerSetService = 0x1A08114u;
constexpr uintptr_t kRvaManagerSeason = 0x1A08038u;

// ---------------------------------------------------------------- constants

// ConfigId.PixelPass, read straight out of the ConfigId enum (TypeDefIndex
// 11085) and cross-checked against the [不丙三且丅上丞丙丏(123, 1, True)] attribute
// on the season DTO. ConfigId 102, the only id the cache is ever asked for on
// this build, is MessagePackTest.
constexpr int32_t kConfigPixelPass = 123;

// OfferItemType members.
constexpr int32_t kTypeWeaponSkin = 1170;
constexpr int32_t kTypeGraffiti = 1470;

// PGCompany.PixelPass.丕专上业上丑专世丗: None=0, First=1, Regular=2, Last=3.
constexpr int32_t kTierFirst = 1;
constexpr int32_t kTierRegular = 2;
constexpr int32_t kTierLast = 3;

constexpr int32_t kTierCount = 50;
constexpr int32_t kTiersPerPage = 10;
constexpr int32_t kTierExp = 100;

// Graffiti keys follow the shape of the system's own "none" sentinel,
// PGCompany.GraffitiSystem.丐且丆世丛下丏丒丏.上东三业专丑三三丁 = "graffiti_-1", so real
// entries are graffiti_<n>. They fill the closing tiers only, which keeps
// every early tier on an id read back from the local skin catalogue.
constexpr bool kIncludeGraffiti = true;
constexpr int32_t kGraffitiTiers = 10;

constexpr size_t kMaxCatalogueEntries = 4096u;
constexpr size_t kMaxIdLength = 96u;

// A stock payload at least this long is treated as real content and always
// wins over the local season.
constexpr int64_t kMinRealPayload = 3;

// Rebuild attempts before the module stops trying (the skin catalogue may
// legitimately be empty on the first few reads).
// Retry budget for building the season. Deliberately generous: the first
// attempt now happens on the loading screen, where the local weapon skin
// catalogue the tier rewards are drawn from is still empty, so the budget has to
// outlive that. An attempt is a managed list read, and they stop the moment one
// succeeds or the managed parse is entered.
constexpr int32_t kMaxBuildAttempts = 240;
// Menu frames between two backstop install attempts from pump().
constexpr uint64_t kInstallRetryFrames = 15u;

// The first report has to land inside a short capture: the facts that matter
// are all visible within a few seconds of the lobby appearing.
constexpr uint64_t kFirstReportFrame = 120u;
constexpr uint64_t kReportPeriodFrames = 1800u;

// Bounded, one-shot-per-pair tracing of which ConfigIds reach which entry
// point. Capped so a chatty pipeline cannot flood the log.
constexpr size_t kMaxNotedPairs = 64u;
constexpr size_t kMaxNoteLines = 24u;

// Kept as a backstop. With a real season and service in place before any gate
// is asked, the stock gates should answer true on their own; if one still does
// not, this opens it -- but only while a season actually exists, because the
// view dereferences it immediately afterwards.
constexpr bool kForceGatesWhenSeasonExists = true;

constexpr const char* kSeasonName = "OPG3D Offline Season";
// Fixed, always-current window: the season must be active whatever the device
// clock says, so no runtime date arithmetic is involved.
constexpr const char* kSeasonStart = "2020-01-01T00:00:00Z";
constexpr const char* kSeasonEnd = "2099-01-01T00:00:00Z";

// Written instead of a quote character so this source stays free of escape
// sequences while emitting JSON.
constexpr char kQuote = static_cast<char>(0x22);
constexpr unsigned char kBackslash = 0x5Cu;

// Il2CppArray keeps its length one word above the bounds pointer: the managed
// object header is 16 bytes, bounds sits at 0x10 and max_length at 0x18.
constexpr size_t kArrayLengthOffset = 0x18u;

// ------------------------------------------------------------- managed ABI

using StaticObjFn = void* (*)(void* method);
using InstanceObjFn = void* (*)(void* self, void* method);
using InstanceBoolFn = bool (*)(void* self, void* method);
using InstanceVoidFn = void (*)(void* self, void* method);
using InstanceIntFn = int32_t (*)(void* self, void* method);
using InstanceIndexFn = void* (*)(void* self, int32_t index, void* method);
using FromBase64Fn = void* (*)(void* text, void* method);
// object JsonConvert.DeserializeObject(string, Type) -- static, so no `this`.
using DeserializeFn = void* (*)(void* json, void* type, void* method);
// void JsonConvert.PopulateObject(string, object) -- static as well.
using PopulateFn = void (*)(void* json, void* target, void* method);
// 三丄三丂丈七业丁丞..ctor(丐丑业丒丈丅丐专丅) and the manager's service setter: both
// instance methods taking one managed reference.
using InstanceArgVoidFn = void (*)(void* self, void* arg, void* method);
// bool 东丗与丏丟丛丂三丞(ConfigId, out byte[], out string) and its identically
// shaped sibling 与丌下丑丝丁丄丏丛 -- instance methods, so `this` first and
// MethodInfo* last; both `out` parameters arrive as pointers.
using LoadConfigFn = bool (*)(void* self, int32_t config_id, void** payload,
                              void** error, void* method);
// bool 丅专东与上丆不丏丐(ConfigId, string, out byte[], out string) -- the private
// loader takes an extra managed string between the id and the outputs.
using LoadConfigPathFn = bool (*)(void* self, int32_t config_id, void* path,
                                  void** payload, void** error, void* method);
// bool 与万丝丗丁不丗一丗(ConfigId, byte[], out string) -- the write path, hooked
// read-only so the log shows what the game itself decides to persist.
using SaveConfigFn = bool (*)(void* self, int32_t config_id, void* payload,
                              void** error, void* method);

struct Managed {
    void* info = nullptr;
    void* ptr = nullptr;
    explicit operator bool() const noexcept {
        return info != nullptr && ptr != nullptr;
    }
};

inline uintptr_t g_base = 0u;

inline bool bind(Managed& out, const char* namespaze, const char* klass,
                 const char* method, int args_count) {
    out.info = il2cpp::find_method_info(namespaze, klass, method, args_count);
    if (out.info == nullptr) {
        LOGE("23.1.3-pixelpass: %s::%s/%d not found in metadata", klass, method,
             args_count);
        return false;
    }
    out.ptr = il2cpp::method_pointer(out.info);
    if (out.ptr == nullptr) {
        LOGE("23.1.3-pixelpass: %s::%s/%d has no method pointer", klass, method,
             args_count);
        return false;
    }
    return true;
}

// Name-and-arity binding plus an identity check against the expected RVA. Safe
// only where the name and argument count already pick out one method, which is
// true for the service .ctor, the manager setter, the season .ctor and
// PopulateObject -- all verified against the dump.
inline bool bind_exact(Managed& out, const char* namespaze, const char* klass,
                       const char* method, int args_count,
                       uintptr_t expected_rva) {
    if (g_base == 0u) {
        LOGE("23.1.3-pixelpass: no module base, so %s::%s/%d cannot be "
             "identified; refusing to call it",
             klass, method, args_count);
        return false;
    }
    if (!bind(out, namespaze, klass, method, args_count)) return false;

    const uintptr_t actual = reinterpret_cast<uintptr_t>(out.ptr) - g_base;
    if (actual != expected_rva) {
        LOGE("23.1.3-pixelpass: %s::%s/%d resolved to RVA 0x%" PRIxPTR
             " but this build expects 0x%" PRIxPTR
             "; refusing it (wrong overload or wrong libil2cpp.so)",
             klass, method, args_count, actual, expected_rva);
        out = Managed{};
        return false;
    }
    return true;
}

// Binding by address, for methods that share a name and an argument count with
// a sibling overload. The method list of the class is walked and the entry
// whose compiled entry point is exactly base + rva is taken, which yields the
// correct pointer and the MethodInfo that belongs to it, together.
//
// This is what the device forced: asking for "DeserializeObject"/2 by name
// returned (string, JsonSerializerSettings) at 0x40BC7C4 instead of
// (string, Type) at 0x40BC82C.
inline bool bind_at_rva(Managed& out, const char* namespaze, const char* klass,
                        uintptr_t rva, const char* label) {
    if (g_base == 0u) {
        LOGE("23.1.3-pixelpass: no module base, so %s cannot be located", label);
        return false;
    }
    if (il2cpp::class_get_methods == nullptr) {
        LOGE("23.1.3-pixelpass: this runtime exposes no method walk, so %s "
             "cannot be picked out of its overloads",
             label);
        return false;
    }

    void* address = reinterpret_cast<void*>(g_base + rva);
    out.info = il2cpp::find_method_by_address(namespaze, klass, address);
    if (out.info == nullptr) {
        LOGE("23.1.3-pixelpass: no method of %s sits at RVA 0x%" PRIxPTR
             ", so %s was not bound",
             klass, rva, label);
        return false;
    }

    out.ptr = il2cpp::method_pointer(out.info);
    if (out.ptr != address) {
        LOGE("23.1.3-pixelpass: the method found for %s does not report the "
             "address it was matched on; refusing it",
             label);
        out = Managed{};
        return false;
    }
    LOGI("23.1.3-pixelpass: %s bound by address at RVA 0x%" PRIxPTR, label, rva);
    return true;
}

// ------------------------------------------------------------------- state

inline Managed g_from_base64{};
inline Managed g_skin_ids{};
inline LoadConfigFn g_load_orig = nullptr;
inline LoadConfigFn g_load_alt_orig = nullptr;
inline LoadConfigPathFn g_load_inner_orig = nullptr;
inline SaveConfigFn g_save_orig = nullptr;
inline bool g_installed = false;
inline bool g_exhausted = false;
inline int32_t g_build_attempts = 0;
inline uint64_t g_frames = 0u;
inline uint64_t g_reads_total = 0u;
inline uint64_t g_queries = 0u;
inline uint64_t g_served = 0u;
inline uint64_t g_stock_wins = 0u;
inline bool g_logged_first_serve = false;
inline size_t g_skin_count = 0u;
inline std::vector<uint64_t> g_noted_pairs;
inline size_t g_note_lines = 0u;
inline size_t g_save_lines = 0u;
// The authored season, kept as text. Managed pointers are NOT cached: without
// a GC handle a stored managed pointer can go stale, so anything managed is
// either handed straight to the game or read back from it on demand.
inline std::string g_season_json;
inline std::string g_season_base64;

// --- pass manager, lobby view, season construction -------------------------

inline Managed g_mgr_instance{};
inline Managed g_mgr_season{};
inline Managed g_mgr_service{};
inline Managed g_mgr_set_service{};
inline Managed g_deserialize{};
inline Managed g_populate{};
inline Managed g_season_ctor{};
inline Managed g_service_ctor{};
inline void* g_season_klass = nullptr;
inline void* g_service_klass = nullptr;
inline InstanceBoolFn g_gate_a_orig = nullptr;
inline InstanceBoolFn g_gate_b_orig = nullptr;
inline InstanceBoolFn g_gate_c_orig = nullptr;
inline InstanceVoidFn g_view_enable_orig = nullptr;

inline void* g_view_holder_field = nullptr;
inline void* g_view_lock_field = nullptr;
inline void* g_view_unlock_field = nullptr;
inline void* g_view_coming_soon_field = nullptr;
inline void* g_view_need_level_field = nullptr;
inline void* g_view_button_field = nullptr;
inline void* g_view_service_field = nullptr;

inline bool g_manager_armed = false;
inline bool g_season_path_ready = false;
inline bool g_route_deserialize = false;
inline bool g_route_populate = false;
// The real point of no return: set immediately before the first managed parse
// call. A managed exception cannot be caught from native frames, so a payload
// the season validator rejects must never be handed over a second time.
//
// This used to be one flag set *before* build_season_object(), which also
// swallowed every transient miss -- an empty skin catalogue on the loading
// screen was enough to disable the module for the whole session and made the
// kMaxBuildAttempts loop dead code.
inline bool g_managed_parse_started = false;
// A terminal refusal that retrying cannot cure: the construction path did not
// verify against this libil2cpp.so.
inline bool g_install_disarmed = false;
inline bool g_install_succeeded = false;
inline uint64_t g_install_misses = 0u;
inline bool g_gate_logged[3] = {false, false, false};
inline bool g_gate_forced_logged[3] = {false, false, false};
inline uint64_t g_gate_forced[3] = {0u, 0u, 0u};
inline uint64_t g_view_enables = 0u;
inline bool g_view_logged = false;
// Guards against a gate hook re-entering itself through the season getter.
inline bool g_in_season_query = false;
// Guards against a season install driven from a gate re-entering that same gate
// through the pass service constructor or the manager's service setter.
inline bool g_in_gate_install = false;

// True once the outcome is decided and another attempt is pointless: the
// service is in place, or managed construction has already been entered once,
// or the path does not verify on this build. Everything else -- most
// importantly a weapon skin catalogue that has not loaded yet -- is a transient
// miss and must stay retryable.
inline bool install_settled() {
    return g_install_succeeded || g_managed_parse_started || g_install_disarmed;
}

// ---------------------------------------------------------------- helpers

// List<T> is reached through its own accessors rather than through hardcoded
// field offsets: the concrete generic instantiation is taken from the returned
// object, so a layout change cannot silently turn into a wild read.
inline bool list_api(void* list, Managed& count, Managed& item) {
    if (list == nullptr || il2cpp::object_get_class == nullptr ||
        il2cpp::class_get_method_from_name == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(list);
    if (klass == nullptr) return false;
    count.info = il2cpp::class_get_method_from_name(klass, "get_Count", 0);
    item.info = il2cpp::class_get_method_from_name(klass, "get_Item", 1);
    if (count.info == nullptr || item.info == nullptr) return false;
    count.ptr = il2cpp::method_pointer(count.info);
    item.ptr = il2cpp::method_pointer(item.info);
    return static_cast<bool>(count) && static_cast<bool>(item);
}

inline void* read_object_field(void* self, void* field) {
    if (self == nullptr || field == nullptr ||
        il2cpp::field_get_value == nullptr) {
        return nullptr;
    }
    void* value = nullptr;
    il2cpp::field_get_value(self, field, &value);
    return value;
}

// A reward id ends up inside a colon-separated token in a JSON string, so any
// id carrying a quote, a backslash, a colon or a control character is dropped
// rather than escaped: dropping one cosmetic keeps the season parseable.
inline bool id_is_emittable(const std::string& id) {
    if (id.empty() || id.size() > kMaxIdLength) return false;
    for (const char raw : id) {
        const unsigned char value = static_cast<unsigned char>(raw);
        if (value < 0x20u) return false;
        if (value == static_cast<unsigned char>(kQuote)) return false;
        if (value == kBackslash) return false;
        if (raw == ':') return false;
    }
    return true;
}

inline void collect_skin_ids(std::vector<std::string>& out) {
    if (!g_skin_ids) return;
    void* list = reinterpret_cast<StaticObjFn>(g_skin_ids.ptr)(g_skin_ids.info);
    if (list == nullptr) return;

    Managed count{};
    Managed item{};
    if (!list_api(list, count, item)) return;

    const int32_t total =
        reinterpret_cast<InstanceIntFn>(count.ptr)(list, count.info);
    if (total <= 0 || static_cast<size_t>(total) > kMaxCatalogueEntries) return;

    out.reserve(static_cast<size_t>(total));
    for (int32_t index = 0; index < total; ++index) {
        void* managed =
            reinterpret_cast<InstanceIndexFn>(item.ptr)(list, index, item.info);
        if (managed == nullptr) continue;
        const std::string id = il2cpp::to_utf8(managed, kMaxIdLength);
        if (!id_is_emittable(id)) continue;
        out.push_back(id);
    }
}

inline void append_key(std::string& json, const char* key) {
    json += kQuote;
    json += key;
    json += kQuote;
    json += ':';
}

inline void append_string(std::string& json, const char* value) {
    json += kQuote;
    json += value;
    json += kQuote;
}

inline void append_reward(std::string& json, int32_t type,
                          const std::string& id, int32_t amount) {
    json += kQuote;
    json += std::to_string(type);
    json += ':';
    json += id;
    json += ':';
    json += std::to_string(amount);
    json += kQuote;
}

// Only fields whose JSON shape is proven from the DTOs are emitted. Anything
// ambiguous (prices, premium flags, elite-task previews) is left out on
// purpose: an omitted property keeps its default, whereas a guessed one can
// fail the whole season parse and put the lobby back to having no pass.
//
// The key names are not guesses -- they are the [JsonProperty] names on the
// season DTO (TypeDefIndex 13225) and on Common (13224) and the tier DTO
// (13234):
//   "c"  Common             "p"  Pages     "l"  Levels    "t"  Tasks
//   "prt" _premiumTaskIndexes         "tb" TasksBase
//   "at" TasksForAds        "r"  GameRewards          "of" Offers
// and inside them:
//   "i"  SeasonId          salted int  -> number
//   "sn" SeasonName        string
//   "s"  StartDate         DateTime    -> ISO-8601 string
//   "e"  EndDate           DateTime    -> ISO-8601 string
//   "vc" VideoDailyCount   salted int  -> number
//   "hc" HintCooldown      plain int   -> number
//   "etr"/"etp"/"tp"       lists       -> []
//   "l"  Level             salted int  -> number
//   "t"  Type              enum        -> number
//   "p"  NumPage           salted int  -> number
//   "e"  Exp               salted int  -> number
//   "r"  Rewards           list of converted strings
//   "f"  IsFree            salted int  -> number  (NOT a bool)
//   "c"  IsCool            real bool   -> true/false
inline std::string build_season(const std::vector<std::string>& skins) {
    const int32_t pages = (kTierCount + kTiersPerPage - 1) / kTiersPerPage;

    std::string json;
    json.reserve(8192u);
    json += '{';

    append_key(json, "c");
    json += '{';
    append_key(json, "i");
    json += "1";
    json += ',';
    append_key(json, "sn");
    append_string(json, kSeasonName);
    json += ',';
    append_key(json, "s");
    append_string(json, kSeasonStart);
    json += ',';
    append_key(json, "e");
    append_string(json, kSeasonEnd);
    json += ',';
    append_key(json, "vc");
    json += "3";
    json += ',';
    append_key(json, "hc");
    json += "60";
    json += ',';
    append_key(json, "etr");
    json += "[]";
    json += ',';
    append_key(json, "etp");
    json += "[]";
    json += ',';
    append_key(json, "tp");
    json += "[]";
    json += '}';
    json += ',';

    append_key(json, "p");
    json += '[';
    for (int32_t page = 0; page < pages; ++page) {
        if (page != 0) json += ',';
        json += '{';
        append_key(json, "p");
        json += std::to_string(page);
        json += ',';
        append_key(json, "pi");
        json += "[]";
        json += '}';
    }
    json += ']';
    json += ',';

    append_key(json, "l");
    json += '[';
    const int32_t graffiti_from =
        kIncludeGraffiti ? (kTierCount - kGraffitiTiers) : kTierCount;
    int32_t graffiti_index = 0;
    for (int32_t tier = 0; tier < kTierCount; ++tier) {
        if (tier != 0) json += ',';
        const int32_t type =
            (tier == 0) ? kTierFirst
                        : ((tier == kTierCount - 1) ? kTierLast : kTierRegular);
        json += '{';
        append_key(json, "l");
        json += std::to_string(tier + 1);
        json += ',';
        append_key(json, "t");
        json += std::to_string(type);
        json += ',';
        append_key(json, "p");
        json += std::to_string(tier / kTiersPerPage);
        json += ',';
        append_key(json, "e");
        json += std::to_string(kTierExp);
        json += ',';
        append_key(json, "r");
        json += '[';
        if (tier >= graffiti_from) {
            const std::string key =
                std::string("graffiti_") + std::to_string(graffiti_index);
            append_reward(json, kTypeGraffiti, key, 1);
            ++graffiti_index;
        } else {
            const size_t slot = static_cast<size_t>(tier) % skins.size();
            append_reward(json, kTypeWeaponSkin, skins[slot], 1);
        }
        json += ']';
        json += ',';
        // Every tier is free on purpose: this port has no store to buy a
        // premium track from, so gating any tier would only hide content.
        // IsFree is a salted int, so this is the number 1 and not `true`.
        append_key(json, "f");
        json += "1";
        json += ',';
        append_key(json, "c");
        json += "false";
        json += '}';
    }
    json += ']';
    json += ',';

    append_key(json, "t");
    json += "[]";
    json += ',';
    append_key(json, "prt");
    json += "[]";
    json += ',';
    append_key(json, "tb");
    json += "[]";
    json += ',';
    append_key(json, "at");
    json += "[]";
    json += ',';
    append_key(json, "of");
    json += "[]";
    json += ',';
    append_key(json, "r");
    json += "{}";
    json += '}';
    return json;
}

// Base64 is only needed by the config-cache safety net, which has to hand the
// runtime a byte[] and has no array allocator to build one with:
// System.Convert.FromBase64String has exactly one overload, whereas
// Encoding.GetBytes has two single-argument ones that metadata lookup cannot
// tell apart. The manager path does not use this -- it passes the raw JSON
// string straight to Newtonsoft.
inline std::string base64(const std::string& raw) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((raw.size() + 2u) / 3u) * 4u);

    size_t index = 0u;
    while (raw.size() >= 3u && index + 3u <= raw.size()) {
        const uint32_t chunk =
            (static_cast<uint32_t>(static_cast<unsigned char>(raw[index])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(raw[index + 1u])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(raw[index + 2u]));
        out += kAlphabet[(chunk >> 18) & 0x3Fu];
        out += kAlphabet[(chunk >> 12) & 0x3Fu];
        out += kAlphabet[(chunk >> 6) & 0x3Fu];
        out += kAlphabet[chunk & 0x3Fu];
        index += 3u;
    }

    const size_t rest = raw.size() - index;
    if (rest == 1u) {
        const uint32_t chunk =
            static_cast<uint32_t>(static_cast<unsigned char>(raw[index])) << 16;
        out += kAlphabet[(chunk >> 18) & 0x3Fu];
        out += kAlphabet[(chunk >> 12) & 0x3Fu];
        out += '=';
        out += '=';
    } else if (rest == 2u) {
        const uint32_t chunk =
            (static_cast<uint32_t>(static_cast<unsigned char>(raw[index])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(raw[index + 1u])) << 8);
        out += kAlphabet[(chunk >> 18) & 0x3Fu];
        out += kAlphabet[(chunk >> 12) & 0x3Fu];
        out += kAlphabet[(chunk >> 6) & 0x3Fu];
        out += '=';
    }
    return out;
}

inline int64_t array_length(void* array) {
    if (array == nullptr) return -1;
    const uint8_t* raw = static_cast<const uint8_t*>(array);
    int32_t length = 0;
    __builtin_memcpy(&length, raw + kArrayLengthOffset, sizeof(length));
    return static_cast<int64_t>(length);
}

// Builds the season text once. Returns false while the local skin catalogue
// is still empty, so the next attempt simply tries again.
inline bool ensure_season_text() {
    if (!g_season_json.empty()) return true;
    if (g_exhausted) return false;

    if (g_build_attempts >= kMaxBuildAttempts) {
        g_exhausted = true;
        LOGE("23.1.3-pixelpass: the local weapon skin catalogue stayed empty "
             "for %" PRId32 " attempts; no season will be built",
             g_build_attempts);
        return false;
    }
    ++g_build_attempts;

    std::vector<std::string> skins;
    collect_skin_ids(skins);
    if (skins.empty()) return false;

    g_season_json = build_season(skins);
    g_season_base64 = base64(g_season_json);
    g_skin_count = skins.size();
    LOGI("23.1.3-pixelpass: season authored (%zu json bytes, %" PRId32
         " tiers, %zu skin ids, graffiti tiers %" PRId32 ")",
         g_season_json.size(), kTierCount, g_skin_count,
         kIncludeGraffiti ? kGraffitiTiers : 0);
    return true;
}

inline void* season_payload() {
    if (!ensure_season_text()) return nullptr;
    if (!g_from_base64 || il2cpp::string_new == nullptr) return nullptr;
    void* text = il2cpp::string_new(g_season_base64.c_str());
    if (text == nullptr) return nullptr;
    return reinterpret_cast<FromBase64Fn>(g_from_base64.ptr)(
        text, g_from_base64.info);
}

// ------------------------------------------------------- pass manager reads

inline void* manager_instance() {
    if (!g_mgr_instance) return nullptr;
    return reinterpret_cast<StaticObjFn>(g_mgr_instance.ptr)(g_mgr_instance.info);
}

// The managed season object the manager is currently holding, or null. The
// recursion guard matters: this is called from inside the gate hooks, and the
// season getter is free to consult anything it likes.
inline void* manager_season(void* manager) {
    if (manager == nullptr || !g_mgr_season || g_in_season_query) return nullptr;
    g_in_season_query = true;
    void* season =
        reinterpret_cast<InstanceObjFn>(g_mgr_season.ptr)(manager, g_mgr_season.info);
    g_in_season_query = false;
    return season;
}

inline void* manager_service(void* manager) {
    if (manager == nullptr || !g_mgr_service) return nullptr;
    return reinterpret_cast<InstanceObjFn>(g_mgr_service.ptr)(manager,
                                                              g_mgr_service.info);
}

// ------------------------------------------------- season object construction

// Turns the authored JSON into a real managed season DTO using the game's own
// Newtonsoft. Doing this by hand is not an option: the DTO graph is full of
// List<T> instantiations and converter-backed salted ints, and this port has
// no generic-instantiation helper to build any of that.
//
// Route 1 parses straight into a typed instance. Route 2 allocates the
// instance, runs its real constructor and lets Newtonsoft fill the fields; it
// needs no method walk, so it is the one that survives a runtime without one,
// and it can also succeed where the typed parse merely returns null.
inline void* build_season_object() {
    if (!g_season_path_ready) return nullptr;
    if (!ensure_season_text()) return nullptr;
    if (il2cpp::string_new == nullptr) return nullptr;

    void* json = il2cpp::string_new(g_season_json.c_str());
    if (json == nullptr) {
        LOGE("23.1.3-pixelpass: the season JSON could not be marshalled");
        return nullptr;
    }

    if (g_route_deserialize && il2cpp::class_get_type != nullptr &&
        il2cpp::type_get_object != nullptr) {
        const void* season_type = il2cpp::class_get_type(g_season_klass);
        void* type_object = (season_type != nullptr)
                                ? il2cpp::type_get_object(season_type)
                                : nullptr;
        if (type_object == nullptr) {
            LOGE("23.1.3-pixelpass: the season type object could not be "
                 "obtained; falling back to allocate and populate");
        } else {
            LOGI("23.1.3-pixelpass: parsing the season with the game's own "
                 "Newtonsoft (%zu json bytes, %" PRId32 " tiers, %zu skin ids)",
                 g_season_json.size(), kTierCount, g_skin_count);
            // Point of no return. From here a managed frame runs and the season
            // validator may throw, which native code cannot catch, so this must
            // never be attempted twice. Everything above this line is a
            // retryable pre-flight check and deliberately does NOT latch.
            g_managed_parse_started = true;
            void* season = reinterpret_cast<DeserializeFn>(g_deserialize.ptr)(
                json, type_object, g_deserialize.info);
            if (season != nullptr) {
                LOGI("23.1.3-pixelpass: the season parsed cleanly");
                return season;
            }
            LOGE("23.1.3-pixelpass: the typed parse returned no season; "
                 "falling back to allocate and populate");
        }
    }

    if (g_route_populate && il2cpp::object_new != nullptr) {
        LOGI("23.1.3-pixelpass: allocating a season and populating it (%zu "
             "json bytes, %" PRId32 " tiers, %zu skin ids)",
             g_season_json.size(), kTierCount, g_skin_count);
        // Same point of no return as the typed route above.
        g_managed_parse_started = true;
        void* season = il2cpp::object_new(g_season_klass);
        if (season == nullptr) {
            LOGE("23.1.3-pixelpass: the season could not be allocated");
            return nullptr;
        }
        // il2cpp_object_new does not run constructors, so this is the `newobj`
        // second half and has to happen before anything reads the instance.
        reinterpret_cast<InstanceVoidFn>(g_season_ctor.ptr)(season,
                                                            g_season_ctor.info);
        reinterpret_cast<PopulateFn>(g_populate.ptr)(json, season,
                                                     g_populate.info);
        LOGI("23.1.3-pixelpass: the season was populated");
        return season;
    }

    LOGE("23.1.3-pixelpass: no season construction route is available");
    return nullptr;
}

// Builds the season, wraps it in a pass service and hands that to the manager.
//
// Attempted exactly once. The flag is set before the first managed call, never
// after: a managed exception cannot be caught from native frames, so a payload
// the validator rejects must not be retried on the next frame.
inline bool install_season(void* manager) {
    if (g_install_succeeded) return true;
    // Only a *decided* outcome blocks another attempt. This used to be a single
    // g_install_attempted latch set before any work happened, which meant the
    // earliest caller -- on the loading screen, before the game has loaded
    // anything the season needs -- permanently disabled the pass.
    if (g_managed_parse_started || g_install_disarmed) return false;
    if (manager == nullptr) return false;

    // Already populated by the game itself: leave it completely alone.
    if (manager_service(manager) != nullptr) {
        g_install_succeeded = true;
        LOGI("23.1.3-pixelpass: the pass manager already holds a service; "
             "nothing was injected");
        return true;
    }

    if (!g_season_path_ready) {
        g_install_disarmed = true;
        LOGE("23.1.3-pixelpass: the season construction path is not armed on "
             "this build; the lobby stays without a pass");
        return false;
    }

    void* season = build_season_object();
    if (season == nullptr) {
        // Unless the parse latch was set, no managed frame ran and this is a
        // transient miss: on the loading screen the local weapon skin catalogue
        // is simply not populated yet. Retry from the next gate query or menu
        // frame rather than giving up on the pass for the whole session.
        if (!g_managed_parse_started) {
            ++g_install_misses;
            if (g_install_misses == 1u) {
                LOGI("23.1.3-pixelpass: no season could be built yet (the local "
                     "weapon skin catalogue is still empty); this is retried, "
                     "not fatal");
            }
        }
        return false;
    }

    LOGI("23.1.3-pixelpass: allocating the pass service");
    void* service = il2cpp::object_new(g_service_klass);
    if (service == nullptr) {
        LOGE("23.1.3-pixelpass: the pass service could not be allocated");
        return false;
    }

    LOGI("23.1.3-pixelpass: running the pass service constructor");
    reinterpret_cast<InstanceArgVoidFn>(g_service_ctor.ptr)(
        service, season, g_service_ctor.info);

    LOGI("23.1.3-pixelpass: handing the pass service to the manager");
    reinterpret_cast<InstanceArgVoidFn>(g_mgr_set_service.ptr)(
        manager, service, g_mgr_set_service.info);

    void* stored_service = manager_service(manager);
    void* stored_season = manager_season(manager);
    g_install_succeeded = (stored_service != nullptr);

    if (!g_install_succeeded) {
        LOGE("23.1.3-pixelpass: the service was constructed but the manager did "
             "not keep it");
        return false;
    }

    if (stored_season != nullptr) {
        LOGI("23.1.3-pixelpass: the pass manager now holds a service and "
             "reports its season; %" PRId32 " tiers over %" PRId32
             " page(s) are live",
             kTierCount, (kTierCount + kTiersPerPage - 1) / kTiersPerPage);
    } else {
        // Worth saying precisely, because it decides where to write next: the
        // manager would then be reading its season from the pass state holder
        // rather than from the service, and that holder needs setting too.
        LOGE("23.1.3-pixelpass: the pass manager kept the service but still "
             "reports no season, so it reads the season from the pass state "
             "holder rather than from the service; that holder is the "
             "remaining place to write");
    }
    return true;
}

// ---------------------------------------------------------- the gate hooks

inline const char* gate_label(int slot) {
    switch (slot) {
        case 0: return "gate A";
        case 1: return "gate B";
        default: return "gate C";
    }
}

// Shared tail for the three manager gates. The stock verdict is always
// reported once. The override is only allowed to open a gate when a season
// actually exists, because the view dereferences it immediately afterwards.
inline bool handle_gate(void* self, int slot, bool stock) {
    // The gates are the earliest moment the game asks about the pass. On the
    // reported capture they are answered ~3.6 s in, from AppsMenu.Start on the
    // loading screen, long before PixelPassLobbyView.OnEnable or the main-menu
    // pump can run. Only observing them deadlocks: gate A answers false, so the
    // lobby never creates the pass entry, so OnEnable never fires, so no season
    // is ever built, so gate A keeps answering false. Build it from here.
    //
    // `self` is the pass manager instance, which is exactly what install_season
    // wants, so no static-instance lookup is needed.
    if (!g_in_gate_install && !install_settled() && g_season_path_ready) {
        static bool s_gate_install_logged = false;
        g_in_gate_install = true;
        if (!s_gate_install_logged) {
            s_gate_install_logged = true;
            LOGI("23.1.3-pixelpass: %s was asked before a season existed; "
                 "building one now",
                 gate_label(slot));
        }
        install_season(self);
        g_in_gate_install = false;
    }

    // Read after the install attempt, so this very call can answer true.
    void* season = manager_season(self);

    if (!g_gate_logged[slot]) {
        g_gate_logged[slot] = true;
        LOGI("23.1.3-pixelpass: the pass manager answered %s = %s while the "
             "season is %s",
             gate_label(slot), stock ? "true" : "false",
             season != nullptr ? "present" : "absent");
    }

    if (stock) return true;
    if (!kForceGatesWhenSeasonExists || season == nullptr) return stock;

    ++g_gate_forced[slot];
    if (!g_gate_forced_logged[slot]) {
        g_gate_forced_logged[slot] = true;
        LOGI("23.1.3-pixelpass: the season exists but %s was shut; opening it "
             "so the lobby pass button can appear",
             gate_label(slot));
    }
    return true;
}

inline bool gate_a_hook(void* self, void* method) {
    const bool stock =
        (g_gate_a_orig != nullptr) ? g_gate_a_orig(self, method) : false;
    return handle_gate(self, 0, stock);
}

inline bool gate_b_hook(void* self, void* method) {
    const bool stock =
        (g_gate_b_orig != nullptr) ? g_gate_b_orig(self, method) : false;
    return handle_gate(self, 1, stock);
}

inline bool gate_c_hook(void* self, void* method) {
    const bool stock =
        (g_gate_c_orig != nullptr) ? g_gate_c_orig(self, method) : false;
    return handle_gate(self, 2, stock);
}

// ------------------------------------------------------ the lobby view hook

// The season is installed here, before the original runs, so the view
// initialises against a populated manager instead of the null it used to find.
// The view's own service field is written too, because the view may have
// cached it in Awake, before this hook ever ran.
inline void view_enable_hook(void* self, void* method) {
    void* manager = manager_instance();
    if (manager != nullptr) install_season(manager);

    if (g_install_succeeded && g_view_service_field != nullptr &&
        il2cpp::field_set_value != nullptr &&
        read_object_field(self, g_view_service_field) == nullptr) {
        void* service = manager_service(manager);
        if (service != nullptr) {
            il2cpp::field_set_value(self, g_view_service_field, &service);
        }
    }

    if (g_view_enable_orig != nullptr) g_view_enable_orig(self, method);
    ++g_view_enables;
    if (g_view_logged) return;
    g_view_logged = true;

    LOGI("23.1.3-pixelpass: PixelPassLobbyView.OnEnable ran (holder=%d "
         "lock=%d unlock=%d coming-soon=%d need-level=%d button=%d "
         "view service=%s; manager=%s manager service=%s season=%s)",
         read_object_field(self, g_view_holder_field) != nullptr ? 1 : 0,
         read_object_field(self, g_view_lock_field) != nullptr ? 1 : 0,
         read_object_field(self, g_view_unlock_field) != nullptr ? 1 : 0,
         read_object_field(self, g_view_coming_soon_field) != nullptr ? 1 : 0,
         read_object_field(self, g_view_need_level_field) != nullptr ? 1 : 0,
         read_object_field(self, g_view_button_field) != nullptr ? 1 : 0,
         read_object_field(self, g_view_service_field) != nullptr ? "set"
                                                                 : "null",
         manager != nullptr ? "alive" : "null",
         manager_service(manager) != nullptr ? "set" : "null",
         manager_season(manager) != nullptr ? "present" : "absent");
}

// --------------------------------------------------------- read path tracing

// Records, once per (entry point, ConfigId) pair, that the stock cache was
// asked for something. On this build the answer turned out to be ConfigId 102
// (MessagePackTest) twice and nothing else, which is what retired the cache as
// a delivery route.
inline void note_config(const char* where, int32_t source, int32_t config_id) {
    const uint64_t key =
        (static_cast<uint64_t>(static_cast<uint32_t>(source)) << 32) |
        static_cast<uint64_t>(static_cast<uint32_t>(config_id));
    for (const uint64_t seen : g_noted_pairs) {
        if (seen == key) return;
    }
    if (g_noted_pairs.size() < kMaxNotedPairs) g_noted_pairs.push_back(key);
    if (g_note_lines >= kMaxNoteLines) return;
    ++g_note_lines;
    LOGI("23.1.3-pixelpass: the stock config cache asked %s for config id "
         "%" PRId32,
         where, config_id);
}

// ------------------------------------------------------------ loader hooks

// Shared tail for all three loaders. `stock` is whatever the original returned.
inline bool handle_load(int32_t config_id, void** payload, void** error,
                        bool stock, const char* where, int32_t source) {
    ++g_reads_total;
    note_config(where, source, config_id);
    if (config_id != kConfigPixelPass) return stock;

    ++g_queries;

    // A real cached season always wins: this module only fills a hole. This is
    // also what makes nested loaders safe -- if the inner hook already served
    // the season, the outer hook sees a real payload and defers to it.
    if (stock && payload != nullptr && *payload != nullptr &&
        array_length(*payload) >= kMinRealPayload) {
        ++g_stock_wins;
        return stock;
    }

    void* season = season_payload();
    if (season == nullptr) return stock;

    if (payload != nullptr) *payload = season;
    if (error != nullptr) *error = nullptr;
    ++g_served;

    if (!g_logged_first_serve) {
        g_logged_first_serve = true;
        LOGI("23.1.3-pixelpass: config %" PRId32 " was empty in the stock "
             "on-device cache; served the local season (%" PRId64 " bytes) "
             "through %s",
             kConfigPixelPass, array_length(season), where);
    }
    return true;
}

inline bool storage_load_hook(void* self, int32_t config_id, void** payload,
                              void** error, void* method) {
    const bool stock =
        (g_load_orig != nullptr)
            ? g_load_orig(self, config_id, payload, error, method)
            : false;
    return handle_load(config_id, payload, error, stock, "the primary loader",
                       0);
}

inline bool storage_load_alt_hook(void* self, int32_t config_id, void** payload,
                                  void** error, void* method) {
    const bool stock =
        (g_load_alt_orig != nullptr)
            ? g_load_alt_orig(self, config_id, payload, error, method)
            : false;
    return handle_load(config_id, payload, error, stock, "the secondary loader",
                       1);
}

inline bool storage_load_inner_hook(void* self, int32_t config_id, void* path,
                                    void** payload, void** error,
                                    void* method) {
    const bool stock =
        (g_load_inner_orig != nullptr)
            ? g_load_inner_orig(self, config_id, path, payload, error, method)
            : false;
    return handle_load(config_id, payload, error, stock, "the inner loader", 2);
}

// Read-only: the write path is never altered, it is only reported.
inline bool storage_save_hook(void* self, int32_t config_id, void* payload,
                              void** error, void* method) {
    const bool stock =
        (g_save_orig != nullptr)
            ? g_save_orig(self, config_id, payload, error, method)
            : false;
    if (g_save_lines < kMaxNoteLines) {
        ++g_save_lines;
        LOGI("23.1.3-pixelpass: the game stored config id %" PRId32
             " itself (%" PRId64 " byte(s), stored=%d)",
             config_id, array_length(payload), stock ? 1 : 0);
    }
    return stock;
}

// ------------------------------------------------------------ diagnostics

inline void pump() {
    if (!g_installed) return;
    ++g_frames;

    // Backstop for anything the gates did not cover, and the retry channel for
    // a season that simply was not buildable yet. Throttled, because the budget
    // is bounded by kMaxBuildAttempts and one attempt per frame would spend all
    // of it in well under a second of menu time -- long before the local weapon
    // skin catalogue is guaranteed to be populated.
    if (!install_settled() && g_season_path_ready &&
        (g_frames % kInstallRetryFrames) == 0u) {
        void* manager = manager_instance();
        if (manager != nullptr) install_season(manager);
    }

    const bool due = (g_frames == kFirstReportFrame) ||
                     ((g_frames % kReportPeriodFrames) == 0u);
    if (!due) return;

    if (!g_manager_armed) {
        LOGI("23.1.3-pixelpass: %" PRIu64 " menu frame(s) in; the pass manager "
             "could not be bound, so only the config cache is instrumented "
             "(cache reads=%" PRIu64 ")",
             g_frames, g_reads_total);
        return;
    }

    void* manager = manager_instance();
    if (manager == nullptr) {
        LOGI("23.1.3-pixelpass: %" PRIu64 " menu frame(s) in, the pass manager "
             "singleton is still null, so nothing about the pass has been "
             "constructed yet",
             g_frames);
        return;
    }

    // Spelled out, because these four states need completely different fixes
    // and the previous "injected=0" could not tell them apart.
    const char* install_state =
        g_install_succeeded
            ? "done"
            : (g_install_disarmed
                   ? "disarmed, the construction path did not verify"
                   : (g_managed_parse_started
                          ? "the managed parse ran once and yielded no usable "
                            "season, so it is not repeated"
                          : "still waiting for a buildable season"));

    LOGI("23.1.3-pixelpass: %" PRIu64 " menu frame(s) in; pass manager alive "
         "(service=%s season=%s), season install=%s (%" PRIu64
         " transient miss(es)), lobby view OnEnable ran "
         "%" PRIu64 " time(s), gates opened by this port A=%" PRIu64
         " B=%" PRIu64 " C=%" PRIu64 "; cache reads=%" PRIu64
         " (config %" PRId32 " reads=%" PRIu64 ")",
         g_frames, manager_service(manager) != nullptr ? "set" : "null",
         manager_season(manager) != nullptr ? "present" : "absent",
         install_state, g_install_misses, g_view_enables, g_gate_forced[0],
         g_gate_forced[1], g_gate_forced[2], g_reads_total, kConfigPixelPass,
         g_queries);

    if (g_view_enables == 0u) {
        LOGI("23.1.3-pixelpass: PixelPassLobbyView.OnEnable has not run once, "
             "so the pass view is not present in this lobby at all");
    }
}

// ------------------------------------------------------------ installation

// Binds everything needed to build a season and hand it over. Every method
// called on this path is identity-checked against its expected RVA first.
inline bool install_season_path() {
    if (il2cpp::object_new == nullptr) {
        LOGE("23.1.3-pixelpass: this runtime does not export managed "
             "allocation, so no season can be constructed");
        return false;
    }

    g_season_klass = il2cpp::find_class(kPassNs, kSeasonClass);
    g_service_klass = il2cpp::find_class(kPassNs, kServiceClass);
    if (g_season_klass == nullptr || g_service_klass == nullptr) {
        LOGE("23.1.3-pixelpass: the season DTO or the pass service class is "
             "missing from metadata");
        return false;
    }

    // Route 1: parse straight into a typed instance. Bound by address, because
    // this name has four two-argument overloads and the device showed metadata
    // lookup returning the wrong one.
    g_route_deserialize =
        (il2cpp::class_get_type != nullptr &&
         il2cpp::type_get_object != nullptr) &&
        bind_at_rva(g_deserialize, kJsonNs, kJsonConvertClass,
                    kRvaDeserializeStringType,
                    "JsonConvert.DeserializeObject(string, Type)");

    // Route 2: allocate, run the real constructor, let Newtonsoft fill the
    // fields. PopulateObject has exactly one two-argument overload and the
    // season .ctor takes none, so name plus arity is unambiguous for both and
    // the RVA check only confirms the build. No method walk is needed here.
    g_route_populate =
        bind_exact(g_populate, kJsonNs, kJsonConvertClass, kPopulateObject, 2,
                   kRvaPopulateObject) &&
        bind_exact(g_season_ctor, kPassNs, kSeasonClass, kCtor, 0,
                   kRvaSeasonCtor);

    if (!g_route_deserialize && !g_route_populate) {
        LOGE("23.1.3-pixelpass: neither season construction route verified "
             "against this build; nothing will be injected");
        return false;
    }

    bool ok = true;
    ok &= bind_exact(g_service_ctor, kPassNs, kServiceClass, kCtor, 1,
                     kRvaServiceCtor);
    ok &= bind_exact(g_mgr_set_service, kPassNs, kManagerClass,
                     kManagerSetService, 1, kRvaManagerSetService);
    if (!ok) {
        LOGE("23.1.3-pixelpass: the pass service constructor or the manager "
             "setter did not verify; nothing will be injected");
        return false;
    }

    LOGI("23.1.3-pixelpass: season construction armed (typed parse=%d, "
         "allocate and populate=%d)",
         g_route_deserialize ? 1 : 0, g_route_populate ? 1 : 0);
    return true;
}

// Binds the pass manager and hooks its gates plus the lobby view.
inline bool install_manager() {
    bool resolved = true;
    resolved &= bind(g_mgr_instance, kPassNs, kManagerClass, kManagerInstance, 0);
    resolved &= bind_exact(g_mgr_season, kPassNs, kManagerClass, kManagerSeason,
                           0, kRvaManagerSeason);
    resolved &= bind(g_mgr_service, kPassNs, kManagerClass, kManagerService, 0);
    if (!resolved) {
        LOGE("23.1.3-pixelpass: the pass manager does not match the expected "
             "23.1.3 metadata; its gates were not touched");
        return false;
    }

    g_season_path_ready = install_season_path();

    const bool gate_a =
        hook::install({kPassNs, kManagerClass, kManagerGateA, 0},
                      reinterpret_cast<void*>(&gate_a_hook),
                      reinterpret_cast<void**>(&g_gate_a_orig), false);
    const bool gate_b =
        hook::install({kPassNs, kManagerClass, kManagerGateB, 0},
                      reinterpret_cast<void*>(&gate_b_hook),
                      reinterpret_cast<void**>(&g_gate_b_orig), false);
    const bool gate_c =
        hook::install({kPassNs, kManagerClass, kManagerGateC, 0},
                      reinterpret_cast<void*>(&gate_c_hook),
                      reinterpret_cast<void**>(&g_gate_c_orig), false);

    // Serialised field names on the view are not obfuscated, so these are
    // resolved by name rather than by offset.
    g_view_holder_field = il2cpp::find_field(kPgNs, kLobbyViewClass, kViewHolder);
    g_view_lock_field = il2cpp::find_field(kPgNs, kLobbyViewClass, kViewLock);
    g_view_unlock_field = il2cpp::find_field(kPgNs, kLobbyViewClass, kViewUnlock);
    g_view_coming_soon_field =
        il2cpp::find_field(kPgNs, kLobbyViewClass, kViewComingSoon);
    g_view_need_level_field =
        il2cpp::find_field(kPgNs, kLobbyViewClass, kViewNeedLevel);
    g_view_button_field =
        il2cpp::find_field(kPgNs, kLobbyViewClass, kViewLobbyButton);
    g_view_service_field =
        il2cpp::find_field(kPgNs, kLobbyViewClass, kViewService);

    const bool view =
        hook::install({kPgNs, kLobbyViewClass, kLobbyViewOnEnable, 0},
                      reinterpret_cast<void*>(&view_enable_hook),
                      reinterpret_cast<void**>(&g_view_enable_orig), false);

    g_manager_armed = true;
    LOGI("23.1.3-pixelpass: pass manager armed (gates A=%d B=%d C=%d, lobby "
         "view OnEnable=%d, season construction=%d); the season is built from "
         "the first gate query that finds none, because the gates are answered "
         "on the loading screen and a shut gate is what removes the lobby entry",
         gate_a ? 1 : 0, gate_b ? 1 : 0, gate_c ? 1 : 0, view ? 1 : 0,
         g_season_path_ready ? 1 : 0);
    return true;
}

inline bool install(uintptr_t base) {
    if (g_installed) return true;
    g_base = base;

    bool resolved = true;
    resolved &= bind(g_from_base64, kSystemNs, kConvertClass, kFromBase64, 1);
    resolved &= bind(g_skin_ids, kRilisoftNs, kSkinCatalogueClass, kSkinIdList, 0);
    if (!resolved) {
        LOGE("23.1.3-pixelpass: metadata does not match the expected 23.1.3 "
             "build; no season will be built");
        return false;
    }

    // Kept as a safety net and as the source of the ConfigId trace. The device
    // has shown the game only ever asks this cache for ConfigId 102, so none of
    // these is required for the module to work.
    const bool primary =
        hook::install({kPgNs, kStorageClass, kStorageLoad, 3},
                      reinterpret_cast<void*>(&storage_load_hook),
                      reinterpret_cast<void**>(&g_load_orig), false);
    const bool alt =
        hook::install({kPgNs, kStorageClass, kStorageLoadAlt, 3},
                      reinterpret_cast<void*>(&storage_load_alt_hook),
                      reinterpret_cast<void**>(&g_load_alt_orig), false);
    const bool inner =
        hook::install({kPgNs, kStorageClass, kStorageLoadInner, 4},
                      reinterpret_cast<void*>(&storage_load_inner_hook),
                      reinterpret_cast<void**>(&g_load_inner_orig), false);
    const bool save =
        hook::install({kPgNs, kStorageClass, kStorageSave, 3},
                      reinterpret_cast<void*>(&storage_save_hook),
                      reinterpret_cast<void**>(&g_save_orig), false);

    install_manager();

    g_installed = true;
    LOGI("23.1.3-pixelpass: armed (config id %" PRId32 ", %" PRId32
         " tiers, %" PRId32 " per page; cache probes primary=%d secondary=%d "
         "inner=%d save=%d, manager=%d, season construction=%d, typed "
         "parse=%d, allocate and populate=%d); the season is constructed on "
         "device and handed to the game's own pass manager, because the device "
         "proved the config cache is never asked for it",
         kConfigPixelPass, kTierCount, kTiersPerPage, primary ? 1 : 0,
         alt ? 1 : 0, inner ? 1 : 0, save ? 1 : 0, g_manager_armed ? 1 : 0,
         g_season_path_ready ? 1 : 0, g_route_deserialize ? 1 : 0,
         g_route_populate ? 1 : 0);
    return true;
}

} // namespace detail

// Builds the offline PixelPass season and gives the game's own pass manager a
// real service built from it, which is what makes the lobby button exist. The
// stock config cache is also instrumented, as a probe and a safety net.
//
// `base` is the loaded libil2cpp.so base address: every managed method this
// module calls is identified against its expected RVA before use, which is
// what keeps the ambiguous JsonConvert overloads apart.
inline bool install_hooks(uintptr_t base) { return detail::install(base); }

// Read-only counters plus a backstop for installing the season if the lobby
// view was enabled before this module was ready. Driven from the main-menu
// Update slot.
inline void pump_from_main_menu() { detail::pump(); }

} // namespace pixel_pass_2313
