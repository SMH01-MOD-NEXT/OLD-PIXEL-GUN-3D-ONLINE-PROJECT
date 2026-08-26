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
// Why the previous approach never worked
// --------------------------------------
// The first attempt granted cosmetics natively and never created a season.
// The second attempt created a season, but wrote it into the on-device cache
// from the MainMenuController.Update slot owned by progression_2313 -- and
// that hook was never installed, because progression_2313 was binding a
// Progress service getter name that does not exist in 23.1.3 metadata and
// bailed out before reaching its hook installation. The supplied logcat shows
// exactly that: "东丝丂丄业丕且丙丑::丞丏业丐丒与业/0 not found in metadata",
// "nothing was hooked", progression=0 -- and, decisively, the pixelpass tag
// reports only its bind phase ("armed") with neither a success line nor the
// "giving up after N attempts" line anywhere in the log. The seeder never ran
// a single time.
//
// Two lessons are baked into the design below.
//
//   1. This module must not depend on another module's hook. It installs its
//      own and is driven by the game, not by a frame counter someone else
//      owns.
//   2. Writing the season into the cache and hoping the config pipeline reads
//      it afterwards is a timing bet. Worse, it is a one-way bet: the old
//      code skipped writing whenever a payload of >= 3 bytes was already
//      cached, so a single malformed season would have been cached forever
//      and would have blocked its own repair.
//
// How the season is supplied now
// ------------------------------
// 23.1.3 ships its own on-device config cache, PGCompany.丅丝业七三丈丝丑丏
// (TypeDefIndex 11078) -- the class that owns the "BinaryConfigStorage.Key"
// marker. Rather than writing into it, this module hooks the read:
//
//   东丗与丏丟丛丂三丞(ConfigId, out byte[], out string)   // RVA 0x249D670
//
// and answers ConfigId 123 with the season whenever the stock lookup comes
// back empty. Any real cached payload always wins, so this cannot mask real
// content. Nothing is persisted, so nothing can rot in the cache. And the
// season arrives exactly when the config pipeline asks for it, whenever that
// happens to be, instead of at a guessed frame number.
//
// Overload safety: the metadata name 东丗与丏丟丛丂三丞 occurs exactly once in the
// whole 23.1.3 dump, so name plus argument count selects it unambiguously --
// unlike the sibling loader 与丌下丑丝丁丄丏丛/3 (0x249E064), which is a different
// name and is left untouched.
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
// Unique in the whole 23.1.3 dump, so name + argc is an unambiguous selector.
constexpr const char* kStorageLoad = "东丗与丏丟丛丂三丞";

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

constexpr uint64_t kReportPeriodFrames = 3600u;

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
// bool 东丗与丏丟丛丂三丞(ConfigId, out byte[], out string) -- instance method, so
// `this` first and MethodInfo* last; both `out` parameters arrive as pointers.
using LoadConfigFn = bool (*)(void* self, int32_t config_id, void** payload,
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
inline bool g_installed = false;
inline bool g_exhausted = false;
inline int32_t g_build_attempts = 0;
inline uint64_t g_frames = 0u;
inline uint64_t g_queries = 0u;
inline uint64_t g_served = 0u;
inline uint64_t g_stock_wins = 0u;
inline bool g_logged_first_serve = false;
inline size_t g_skin_count = 0u;
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

// ---------------------------------------------------------------- the hook

inline bool storage_load_hook(void* self, int32_t config_id, void** payload,
                              void** error, void* method) {
    const bool stock =
        (g_load_orig != nullptr)
            ? g_load_orig(self, config_id, payload, error, method)
            : false;
    if (config_id != kConfigPixelPass) return stock;

    ++g_queries;

    // A real cached season always wins: this module only fills a hole.
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
             "on-device cache; served the local season (%" PRId64 " bytes) -- "
             "the lobby pass button and its tiers exist from here on",
             kConfigPixelPass, array_length(season));
    }
    return true;
}

// ------------------------------------------------------------ diagnostics

inline void pump() {
    if (!g_installed) return;
    ++g_frames;
    if ((g_frames % kReportPeriodFrames) != 0u) return;
    LOGI("23.1.3-pixelpass: config %" PRId32 " reads=%" PRIu64
         " served locally=%" PRIu64 " stock payload won=%" PRIu64
         " skin ids=%zu",
         kConfigPixelPass, g_queries, g_served, g_stock_wins, g_skin_count);
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

    g_installed = true;
    LOGI("23.1.3-pixelpass: armed on the config cache read path (config id "
         "%" PRId32 ", %" PRId32 " tiers, %" PRId32 " per page); the season is "
         "served on demand and nothing is persisted",
         kConfigPixelPass, kTierCount, kTiersPerPage);
    return true;
}

} // namespace detail

// Hooks the stock on-device config cache read and binds the base64 decoder and
// the local weapon skin catalogue. The season itself is authored lazily, on
// the first config read that comes back empty, where a game thread and a
// populated skin catalogue are both available.
inline bool install_hooks() { return detail::install(); }

// Optional read-only counters, driven from the main-menu Update slot when that
// slot happens to be available. The season does NOT depend on this being
// called: that dependency is exactly what kept the pass missing before.
inline void pump_from_main_menu() { detail::pump(); }

} // namespace pixel_pass_2313
