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
// Why there was no battle pass at all
// -----------------------------------
// PGCompany.PixelPassLobbyView (TypeDefIndex 12069) owns the lobby entry
// point. It keeps a whole set of states behind separate containers --
// _lockContainer (0x70), _unLockContainer (0x78), _comingSoonContainer
// (0x80), _needLevelContainer (0x88), _tutorialContainer (0xA0) -- but all of
// them live under a single _holder (0x48), and the view switches that holder
// off wholesale when the pass service (三丄三丂丈七业丁丞, held at +0x110) has no
// season. So with no season there is no button at all, not even a
// coming-soon state.
//
// The season is pure configuration. PGCompany.PixelPass.丐丑业丒丈丅丐专丅
// (TypeDefIndex 13225) is tagged [不丙三且丅上丞丙丏(123, 1, True)] together with
// [JsonObject(1)] and carries the entire pass: Common ("c"), Pages ("p"),
// Levels ("l"), tasks and offers. ConfigId 123 is PixelPass. The retired
// backend never computed that payload, it only shipped it -- so it can be
// supplied locally.
//
// History of this module, and what each device log actually proved
// ---------------------------------------------------------------
// Attempt 1 granted cosmetics natively and never created a season at all.
//
// Attempt 2 created a season, but wrote it into the on-device cache from the
// MainMenuController.Update slot owned by progression_2313 -- and that hook
// was never installed, because progression_2313 bound a Progress service
// getter name that does not exist in 23.1.3 metadata and bailed out before
// reaching its hook installation. The first device log showed exactly that:
// "东丝丂丄业丕且丙丑::丞丏业丐丒与业/0 not found in metadata", "nothing was hooked",
// progression=0. The seeder never ran a single time.
//
// Attempt 3 -- the read hook below -- fixed that dependency and moved the
// season onto the config pipeline's own read path. The 89 second device log
// taken afterwards proves the repair worked and that the remaining problem is
// somewhere else entirely:
//
//   * "23.1.3-progression: installed", then at runtime "armed; coin
//     key='Coins' ... level=65" and "max level reached; experience topped up
//     999995 -> 900000000". Both of those runtime lines come out of the
//     MainMenuController.Update pump, so the pump is alive.
//   * "23.1.3-trace: MAIN MENU REACHED", and the sibling pumps report from the
//     same slot: modules pass 1 complete, hidden-items pass 1 complete,
//     content "opens 21/21 curated content features on its own (gate
//     queries=78434)". The PixelPass feature gate is open.
//   * Zero E/OPG3D lines in the entire capture.
//   * And the pixelpass tag prints exactly one line: its own "armed". No
//     "season authored", no "served the local season".
//
// The hook is installed and the game is in the lobby, yet the season was
// never authored. Authoring happens lazily inside the read hook, so the only
// possible reading is that 东丗与丏丟丛丂三丞/3 was never entered with ConfigId 123.
//
// Why one read hook was not enough
// --------------------------------
// The stock cache class PGCompany.丅丝业七三丈丝丑丏 (TypeDefIndex 11078, the class
// that owns the "BinaryConfigStorage.Key" marker) does not have one read entry
// point. It has three, and two of them share an identical signature:
//
//   内 东丗与丏丟丛丂三丞(ConfigId, out byte[], out string)            // 0x249D670
//   内 与丌下丑丝丁丄丏丛(ConfigId, out byte[], out string)            // 0x249E064
//   内 丅专东与上丆不丏丐(ConfigId, string, out byte[], out string)   // 0x249DACC, private
//
// Attempt 3 hooked only the first because its metadata name occurs once in
// the dump -- but uniqueness of a name says nothing about which overload the
// config pipeline calls. All three are covered now, and the season is served
// from whichever one the game actually uses.
//
// Nesting resolves itself: if a public loader delegates to the private one,
// the inner hook serves the season first, and the outer hook then sees a
// payload that is already real and lets it through untouched.
//
// Reading the pipeline from the other end also shows why the cache may simply
// never be consulted for 123. The generic holder PGCompany.东丈与专专丈丘七丄<T>
// (11083) keeps a Dictionary<ConfigId, holder> plus one cached T, and its
// slot-5 virtual is 丆丕丙丛丑不丟丁丗(object, 不丟丌丌丌世丅丄与) -- an event handler,
// where 不丟丌丌丌世丅丄与 (11081) is an EventArgs carrying a ConfigId and a string.
// BalanceController has the matching pieces: 丑不业丑专专丝专丝(object,
// 不丟丌丌丌世丅丄与) at 0x471BF94, 丛丐丄一丞不且丂下(ConfigId) at 0x471C074, the gate
// 丁丁丁丏丟丄丘丄专(ConfigId) at 0x471CAC0, and 世丄丅丏丌专上世丄(string, byte[],
// 丐丛丏丒丘东三专一, ConfigId) at 0x471CB40. So configs are pushed in when bytes
// arrive and an event is raised -- and in this port no bytes ever arrive for
// 123: the log shows the emulated backend answering every config request
// (rating maps, BuffSettings, rating minigame) with 200 and two bytes, and
// nothing at all requesting a pass.
//
// That is the question the diagnostics below answer without another guess. If
// the report line shows the cache was read at least once, the read path is
// live and only the ConfigId set matters. If it shows zero reads while the
// lobby is up, the cache is never consulted and the season has to be supplied
// one level higher, at the pass manager PGCompany.PixelPass.万丈丏丈丙丑万万丙
// (season getter 丒不丏一丂丈丙东丟 at 0x1A08038, gates at 0x1A0811C / 0x1A0815C /
// 0x1A08460, config sink 一丐且专丕丐丝丕业 at 0x1A09820).
//
// Two properties of the build make hand-authored JSON safe here:
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
// One risk stays explicitly unproven. The season is validated by
// DataSystem.DataValidation.FluentValidators.丅丑世丈世七丈丂丁, an
// AbstractValidator<丐丑业丒丈丅丐专丅> (TypeDefIndex 7760, ctor 0x2D484D0), with
// sibling validators for the Common (7762) and Page (7764) DTOs. Those rules
// live in constructor bodies, which a metadata dump does not contain, so a
// deliberately minimal season could parse and still be rejected. If the
// diagnostics show the season being served and the button still does not
// appear, that validator is the next thing to look at.
//
// Only ids the build itself reports are written for skins: they come from
// Rilisoft.与世且一丁丆丈丄丈.丛上丌丏丟丒东丂且(), which is backed by the local
// WeaponSkins resource and is therefore populated with no network at all.
//
// Everything is fail-closed: if a metadata target is missing, if the skin
// catalogue is still empty, or if the payload cannot be marshalled, the stock
// result is returned untouched and the next read tries again.
namespace pixel_pass_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ----------------------------------------------------------- metadata names

constexpr const char* kPgNs = "PGCompany";
constexpr const char* kRilisoftNs = "Rilisoft";
constexpr const char* kSystemNs = "System";

constexpr const char* kStorageClass = "丅丝业七三丈丝丑丏";
constexpr const char* kStorageInstance = "下丌丑丁下丟丛丘上";

// The three read entry points of the stock cache, plus the write path. Every
// one of them is covered: which overload the config pipeline prefers is not
// something a metadata dump can answer.
constexpr const char* kStorageLoad = "东丗与丏丟丛丂三丞";       // /3, RVA 0x249D670
constexpr const char* kStorageLoadAlt = "与丌下丑丝丁丄丏丛";    // /3, RVA 0x249E064
constexpr const char* kStorageLoadInner = "丅专东与上丆不丏丐";  // /4, RVA 0x249DACC
constexpr const char* kStorageSave = "与万丝丗丁不丗一丗";       // /3, RVA 0x249CD64

constexpr const char* kConvertClass = "Convert";
constexpr const char* kFromBase64 = "FromBase64String";

constexpr const char* kSkinCatalogueClass = "与世且一丁丆丈丄丈";
constexpr const char* kSkinIdList = "丛上丌丏丟丒东丂且";

// ---------------------------------------------------------------- constants

// ConfigId.PixelPass, read straight out of the ConfigId enum (TypeDefIndex
// 11085) and cross-checked against the [不丙三且丅上丞丙丏(123, 1, True)] attribute
// on the season DTO.
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
constexpr int32_t kMaxBuildAttempts = 32;

// The previous build only reported once every 3600 menu frames, which is
// about a minute of lobby time -- long enough that a normal capture ended
// before the one line that says whether the read path is used at all. The
// first report now lands within a few seconds of the menu appearing.
constexpr uint64_t kFirstReportFrame = 120u;
constexpr uint64_t kReportPeriodFrames = 1800u;

// Bounded, one-shot-per-pair tracing of which ConfigIds reach which entry
// point. Capped so a chatty pipeline cannot flood the log.
constexpr size_t kMaxNotedPairs = 64u;
constexpr size_t kMaxNoteLines = 24u;

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
using InstanceIntFn = int32_t (*)(void* self, void* method);
using InstanceIndexFn = void* (*)(void* self, int32_t index, void* method);
using FromBase64Fn = void* (*)(void* text, void* method);
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
// The season is built once and kept as base64 text. A managed byte[] is NOT
// cached: without a GC handle a stored managed pointer can be moved or
// collected, so the array is re-created from this text on every read instead.
inline std::string g_season_base64;

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
// Verified against Common (TypeDefIndex 13224) and the tier DTO (13234):
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

// The payload has to reach managed code as a byte[]. The IL2CPP wrapper this
// port owns has no array allocator, and System.Text.Encoding.GetBytes has two
// single-argument overloads (char[] and string) that metadata lookup by name
// and argument count cannot tell apart -- picking the wrong one would hand a
// string to a char[] parameter. System.Convert.FromBase64String has exactly
// one overload, so the JSON is base64-encoded here and decoded by the runtime.
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
// is still empty, so the next config read simply tries again.
inline bool ensure_season_text() {
    if (!g_season_base64.empty()) return true;
    if (g_exhausted) return false;

    if (g_build_attempts >= kMaxBuildAttempts) {
        g_exhausted = true;
        LOGE("23.1.3-pixelpass: the local weapon skin catalogue stayed empty "
             "for %" PRId32 " config reads; no season will be served",
             g_build_attempts);
        return false;
    }
    ++g_build_attempts;

    std::vector<std::string> skins;
    collect_skin_ids(skins);
    if (skins.empty()) return false;

    const std::string json = build_season(skins);
    g_season_base64 = base64(json);
    g_skin_count = skins.size();
    LOGI("23.1.3-pixelpass: season authored (%zu json bytes, %" PRId32
         " tiers, %zu skin ids, graffiti tiers %" PRId32 ")",
         json.size(), kTierCount, g_skin_count,
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

// --------------------------------------------------------- read path tracing

// Records, once per (entry point, ConfigId) pair, that the stock cache was
// asked for something. This is the line that decides where the season has to
// be supplied: if it never appears while the lobby is up, the cache is not on
// the path at all and hooking any loader is pointless.
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

// ---------------------------------------------------------------- the hooks

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
             "through %s -- the lobby pass button and its tiers exist from "
             "here on",
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

// Read-only: the write path is never altered, it is only reported. If the game
// ever persists ConfigId 123 itself, that payload is real content and the
// loaders above will hand it back in preference to the local season.
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
    const bool due = (g_frames == kFirstReportFrame) ||
                     ((g_frames % kReportPeriodFrames) == 0u);
    if (!due) return;
    LOGI("23.1.3-pixelpass: %" PRIu64 " menu frame(s) in, the stock config "
         "cache was read %" PRIu64 " time(s) in total; config %" PRId32
         " reads=%" PRIu64 " served locally=%" PRIu64
         " stock payload won=%" PRIu64 " skin ids=%zu",
         g_frames, g_reads_total, kConfigPixelPass, g_queries, g_served,
         g_stock_wins, g_skin_count);
    if (g_reads_total == 0u) {
        LOGI("23.1.3-pixelpass: the lobby is up and the stock config cache has "
             "not been read once, so the season cannot be delivered through it "
             "on this build; the pass manager is the next place to supply it");
    }
}

// ------------------------------------------------------------ installation

inline bool install() {
    if (g_installed) return true;

    bool resolved = true;
    resolved &= bind(g_from_base64, kSystemNs, kConvertClass, kFromBase64, 1);
    resolved &= bind(g_skin_ids, kRilisoftNs, kSkinCatalogueClass, kSkinIdList, 0);
    if (!resolved) {
        LOGE("23.1.3-pixelpass: metadata does not match the expected 23.1.3 "
             "build; no season will be served");
        return false;
    }

    // The season is served from the cache read itself, so this module owns its
    // own hook and is driven by the game's own config pipeline. It no longer
    // depends on any other module's Update slot.
    if (!hook::install({kPgNs, kStorageClass, kStorageLoad, 3},
                       reinterpret_cast<void*>(&storage_load_hook),
                       reinterpret_cast<void**>(&g_load_orig), true)) {
        LOGE("23.1.3-pixelpass: the config cache read %s::%s/3 could not be "
             "hooked; the lobby stays without a pass",
             kStorageClass, kStorageLoad);
        return false;
    }

    // The remaining entry points are best effort: the previous build proved
    // that hooking one loader is not enough, but none of them is individually
    // required for the module to work.
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

    g_installed = true;
    LOGI("23.1.3-pixelpass: armed on every config cache read path (config id "
         "%" PRId32 ", %" PRId32 " tiers, %" PRId32 " per page; primary=1 "
         "secondary=%d inner=%d save-probe=%d); the season is served on demand "
         "and nothing is persisted",
         kConfigPixelPass, kTierCount, kTiersPerPage, alt ? 1 : 0,
         inner ? 1 : 0, save ? 1 : 0);
    return true;
}

} // namespace detail

// Hooks every read path of the stock on-device config cache and binds the
// base64 decoder and the local weapon skin catalogue. The season itself is
// authored lazily, on the first config read that comes back empty, where a
// game thread and a populated skin catalogue are both available.
inline bool install_hooks() { return detail::install(); }

// Read-only counters, driven from the main-menu Update slot. The season does
// NOT depend on this being called: that dependency is exactly what kept the
// pass missing two attempts ago. It now reports early, because the one fact
// that matters -- whether the stock config cache is read at all while the
// lobby is up -- has to survive a short capture.
inline void pump_from_main_menu() { detail::pump(); }

} // namespace pixel_pass_2313
