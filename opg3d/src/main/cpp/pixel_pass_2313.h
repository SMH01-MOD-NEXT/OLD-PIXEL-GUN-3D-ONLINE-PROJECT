#pragma once

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "il2cpp.h"
#include "log.h"

// Offline PixelPass season provisioning for the exact supplied 23.1.3 ARM64
// libil2cpp.so.
//
// Why there was no battle pass at all
// -----------------------------------
// PGCompany.PixelPassLobbyView (TypeDefIndex 12069) owns the lobby entry
// point. It keeps a whole set of states behind separate containers --
// _lockContainer (0x70), _unLockContainer (0x78), _comingSoonContainer
// (0x80), _needLevelContainer (0x88) -- but all of them live under a single
// _holder (0x48), and the view switches that holder off wholesale when the
// pass service (三丄三丂丈七业丁丞, held at +0x110) has no season. That is why the
// main menu showed no pass button at all, not even a coming-soon state: with
// no season there is nothing to lay out.
//
// The season is pure configuration. PGCompany.PixelPass.丐丑业丒丈丅丐专丅
// (TypeDefIndex 13225) is tagged [不丙三且丅上丞丙丏(123, 1, True)] together with
// [JsonObject(1)] and carries the entire pass: Common ("c"), Pages ("p"),
// Levels ("l"), tasks and offers. ConfigId 123 is the payload the stock
// client calls "pixel-pass-v6". The retired backend never computed it, it
// only shipped it -- so it can be supplied locally.
//
// How the season is supplied, inside this library
// ----------------------------------------------
// 23.1.3 ships its own on-device config cache: PGCompany.丅丝业七三丈丝丑丏
// (TypeDefIndex 11078), the class that owns the "BinaryConfigStorage.Key"
// marker. It exposes an instance save and an instance load keyed by ConfigId:
//
//   与万丝丗丁不丗一丗(ConfigId, byte[], out string)     // save
//   东丗与丏丟丛丂三丞(ConfigId, out byte[], out string)  // load
//
// So the season is handed to the game through its own storage, parsed by its
// own Newtonsoft pipeline and rendered by its own pass screens. No self-hosted
// backend, no reimplemented UI, no synthetic managed objects.
//
// Two properties of the build make hand-authored JSON safe here:
//
//   * Rilisoft.丅丏丏丛丕丁丟上丞, the salted int used for SeasonId and tier Exp, is
//     tagged [JsonConverter(typeof(七不不丐专世丝丄上))]. In JSON it is therefore a
//     plain number and the converter re-salts it on read: no salt is ever
//     fabricated, so nothing looks tampered with to the client.
//   * Rewards are strings. PGCompany.丏不丏丂丙丐专丏丅.ReadJson (0x33494A4) hands
//     the token to DataSystem.DataCollectors.丒丗丘万一七与丟丕.丌丄丛丈与丝丑世丆
//     (0x2B005A4), and 丑一丘与丁丄专专专.丅专万三丙业丗丟一 (0x24B4260) splits the string
//     on ':' (movz w1, #0x3A) and int-parses the first field, which matches the
//     (OfferItemType, string id, int amount) constructor at 0x24B39D8. A reward
//     is therefore "<type>:<id>:<amount>", e.g. "1170:<skin id>:1".
//
// Only ids the build itself reports are written for skins: they come from
// Rilisoft.与世且一丁丆丈丄丈.丛上丌丏丟丒东丂且(), which is backed by the local
// WeaponSkins resource and is therefore populated with no network at all.
//
// Everything is fail-closed and idempotent. If a metadata target is missing,
// if the skin catalogue is empty, or if a non-empty season payload is already
// cached, nothing is written.
namespace pixel_pass_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

constexpr const char* kPgNs = "PGCompany";
constexpr const char* kRilisoftNs = "Rilisoft";
constexpr const char* kSystemNs = "System";

constexpr const char* kStorageClass = "丅丝业七三丈丝丑丏";
constexpr const char* kStorageInstance = "下丌丑丁下丟丛丘上";
constexpr const char* kStorageSave = "与万丝丗丁不丗一丗";
constexpr const char* kStorageLoad = "东丗与丏丟丛丂三丞";

constexpr const char* kConvertClass = "Convert";
constexpr const char* kFromBase64 = "FromBase64String";

constexpr const char* kSkinCatalogueClass = "与世且一丁丆丈丄丈";
constexpr const char* kSkinIdList = "丛上丌丏丟丒东丂且";

// ConfigId.PixelPass, proven from the ConfigId enum and the payload-name
// table in PGCompany.丗且三丕上业丐丕丄 ("pixel-pass-v6").
constexpr int32_t kConfigPixelPass = 123;

// OfferItemType members (dump line 364118 onwards).
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
constexpr size_t kMaxErrorLength = 256u;
constexpr uint64_t kWarmupFrames = 240u;
constexpr uint64_t kRetryIntervalFrames = 600u;
constexpr int32_t kMaxAttempts = 5;
constexpr int64_t kMinExistingPayload = 3;

constexpr const char* kSeasonName = "OPG3D Offline Season";
// Fixed, always-current window: the season must be active whatever the device
// clock says, so no runtime date arithmetic is involved.
constexpr const char* kSeasonStart = "2020-01-01T00:00:00Z";
constexpr const char* kSeasonEnd = "2099-01-01T00:00:00Z";

// Written instead of a quote character so this source stays free of escape
// sequences while emitting JSON.
constexpr char kQuote = static_cast<char>(0x22);
constexpr unsigned char kBackslash = 0x5Cu;

// Il2CppArray keeps its length one word above the vector; only the low 32 bits
// are ever needed here and the read is bounds-free by construction because the
// pointer comes straight out of the managed load call.
constexpr size_t kArrayLengthOffset = 0x18u;

using StaticObjFn = void* (*)(void* method);
using InstanceIntFn = int32_t (*)(void* self, void* method);
using InstanceIndexFn = void* (*)(void* self, int32_t index, void* method);
using FromBase64Fn = void* (*)(void* text, void* method);
using SaveConfigFn = bool (*)(void* self, int32_t config_id, void* payload,
                              void** error, void* method);
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

inline Managed g_storage_instance{};
inline Managed g_storage_save{};
inline Managed g_storage_load{};
inline Managed g_from_base64{};
inline Managed g_skin_ids{};
inline bool g_installed = false;
inline bool g_seeded = false;
inline bool g_disabled = false;
inline int32_t g_attempts = 0;
inline uint64_t g_frames = 0u;

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
    void* list =
        reinterpret_cast<StaticObjFn>(g_skin_ids.ptr)(g_skin_ids.info);
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
inline std::string build_season(const std::vector<std::string>& skins) {
    const int32_t pages =
        (kTierCount + kTiersPerPage - 1) / kTiersPerPage;

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
        const int32_t type = (tier == 0)
                                 ? kTierFirst
                                 : ((tier == kTierCount - 1) ? kTierLast
                                                             : kTierRegular);
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
            const size_t slot =
                static_cast<size_t>(tier) % skins.size();
            append_reward(json, kTypeWeaponSkin, skins[slot], 1);
        }
        json += ']';
        json += ',';
        // Every tier is free on purpose: this port has no store to buy a
        // premium track from, so gating any tier would only hide content.
        append_key(json, "f");
        json += "true";
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
// string to a char[] parameter. System.Convert.FromBase64String has exactly one
// overload, so the JSON is base64-encoded here and decoded by the runtime.
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

inline void* managed_payload(const std::string& json) {
    if (!g_from_base64 || il2cpp::string_new == nullptr) return nullptr;
    const std::string encoded = base64(json);
    void* text = il2cpp::string_new(encoded.c_str());
    if (text == nullptr) return nullptr;
    return reinterpret_cast<FromBase64Fn>(g_from_base64.ptr)(
        text, g_from_base64.info);
}

inline int64_t array_length(void* array) {
    if (array == nullptr) return -1;
    const uint8_t* raw = static_cast<const uint8_t*>(array);
    int32_t length = 0;
    __builtin_memcpy(&length, raw + kArrayLengthOffset, sizeof(length));
    return static_cast<int64_t>(length);
}

inline void* storage() {
    if (!g_storage_instance) return nullptr;
    return reinterpret_cast<StaticObjFn>(g_storage_instance.ptr)(
        g_storage_instance.info);
}

inline int64_t cached_payload_length(void* self) {
    if (self == nullptr || !g_storage_load) return -1;
    void* payload = nullptr;
    void* error = nullptr;
    const bool loaded = reinterpret_cast<LoadConfigFn>(g_storage_load.ptr)(
        self, kConfigPixelPass, &payload, &error, g_storage_load.info);
    if (!loaded || payload == nullptr) return 0;
    return array_length(payload);
}

inline void seed() {
    void* self = storage();
    if (self == nullptr) {
        LOGE("23.1.3-pixelpass: config storage instance not available yet");
        return;
    }

    const int64_t existing = cached_payload_length(self);
    if (existing >= kMinExistingPayload) {
        g_seeded = true;
        LOGI("23.1.3-pixelpass: season config %" PRId32
             " already cached (%" PRId64 " bytes); left untouched",
             kConfigPixelPass, existing);
        return;
    }

    std::vector<std::string> skins;
    collect_skin_ids(skins);
    if (skins.empty()) {
        LOGE("23.1.3-pixelpass: local weapon skin catalogue is still empty; "
             "refusing to write a season with no rewards");
        return;
    }

    const std::string json = build_season(skins);
    void* payload = managed_payload(json);
    if (payload == nullptr) {
        LOGE("23.1.3-pixelpass: season payload could not be marshalled");
        return;
    }

    void* error = nullptr;
    const bool saved = reinterpret_cast<SaveConfigFn>(g_storage_save.ptr)(
        self, kConfigPixelPass, payload, &error, g_storage_save.info);
    if (!saved) {
        const std::string reason =
            (error != nullptr) ? il2cpp::to_utf8(error, kMaxErrorLength)
                               : std::string("no reason reported");
        LOGE("23.1.3-pixelpass: config storage refused the season: %s",
             reason.c_str());
        return;
    }

    const int64_t stored = cached_payload_length(self);
    g_seeded = true;
    LOGI("23.1.3-pixelpass: season written to the stock config cache "
         "(id %" PRId32 ", %zu json bytes, %" PRId64 " bytes cached, "
         "%" PRId32 " tiers, %zu skin ids, graffiti tiers %" PRId32 ")",
         kConfigPixelPass, json.size(), stored, kTierCount, skins.size(),
         kIncludeGraffiti ? kGraffitiTiers : 0);
    LOGI("23.1.3-pixelpass: first reward token is %s",
         json.find(std::to_string(kTypeWeaponSkin)) == std::string::npos
             ? "absent"
             : "present");
}

inline void pump() {
    if (!g_installed || g_seeded || g_disabled) return;
    ++g_frames;
    if (g_frames < kWarmupFrames) return;
    if (((g_frames - kWarmupFrames) % kRetryIntervalFrames) != 0u) return;

    if (g_attempts >= kMaxAttempts) {
        g_disabled = true;
        LOGE("23.1.3-pixelpass: giving up after %" PRId32
             " attempts; no season was written", g_attempts);
        return;
    }
    ++g_attempts;
    seed();
}

inline bool install() {
    if (g_installed) return true;

    bool resolved = true;
    resolved &= bind(g_storage_instance, kPgNs, kStorageClass, kStorageInstance, 0);
    resolved &= bind(g_storage_save, kPgNs, kStorageClass, kStorageSave, 3);
    resolved &= bind(g_storage_load, kPgNs, kStorageClass, kStorageLoad, 3);
    resolved &= bind(g_from_base64, kSystemNs, kConvertClass, kFromBase64, 1);
    resolved &= bind(g_skin_ids, kRilisoftNs, kSkinCatalogueClass, kSkinIdList, 0);
    if (!resolved) {
        LOGE("23.1.3-pixelpass: metadata does not match the expected 23.1.3 "
             "build; no season will be written");
        return false;
    }

    g_installed = true;
    LOGI("23.1.3-pixelpass: armed (config id %" PRId32 ", %" PRId32
         " tiers, %" PRId32 " per page)",
         kConfigPixelPass, kTierCount, kTiersPerPage);
    return true;
}

} // namespace detail

// Binds the stock config cache, the base64 decoder and the local weapon skin
// catalogue. Nothing is written at install time: the season is authored from
// the main menu, where a game thread and a settled managed heap are available.
inline bool install_hooks() { return detail::install(); }

// Called from the main-menu Update slot this port already owns. Runs at most
// once per launch, and only until the season is cached.
inline void pump_from_main_menu() { detail::pump(); }

} // namespace pixel_pass_2313
