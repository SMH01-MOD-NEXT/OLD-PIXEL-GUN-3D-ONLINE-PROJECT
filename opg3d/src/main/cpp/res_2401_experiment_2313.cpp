// 23.1.3 experiment: ask the shipped Downloader for the 24.0.1 resource set.
//
// How 23.1.3 addresses its resources (from the 23.1.3 il2cpp dump):
//
//   ConfigId picker            PGCompany.AssetBundles_v3 url helper, 0x1D25128
//                              -> AssetBundlesIos = 152 / AssetBundlesAndroid = 153
//   config payload parser      0x1D36CDC, string -> List<name+hash>
//   platform resource path     0x1D39130, (platform, string) -> string
//   per-bundle Uri factory     0x1D35758, name+hash -> Uri
//                              (UriBuilder(scheme, "pixelgun3d.akamaized.net")
//                               + set_Path, so bundles are content addressed
//                               by name and hash)
//   queue hand-off             AndroidNativeAssetBundleDownloader
//                              .SetLoadingQueue, 0x1D2795C, then the Java
//                              bridge com.lightmap.assetbundledownload.Bridge
//
// The bundle list is therefore whatever the version-keyed config says it is.
// This module logs that list and, where a version token actually appears in it,
// swaps 23.1.3 for 24.0.1. Every rewrite is fail-open: when the token is absent
// or the rewritten managed string does not round-trip, the original string is
// handed back to the game and the client downloads its own resources as usual.
//
// UpdatesChecker (TypeDefIndex 6204) is deliberately untouched.

#include "res_2401_experiment_2313.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <pthread.h>
#include <unistd.h>

#include "elf_sym.h"
#include "hook.h"
#include "il2cpp.h"
#include "il2cpp_runtime_2313.h"
#include "log.h"

namespace res_2401_experiment_2313 {
namespace {

constexpr const char* kTag = "23.1.3-res-2401";

// ------------------------------------------------------------- experiment ---

// Master switch. Set to false to keep the logging and stop retargeting.
constexpr bool kEnabled = true;

// Swap the version token inside the bundle config payload.
constexpr bool kRewriteConfigPayload = true;

// Swap the version token inside the composed per-platform resource path.
constexpr bool kRewriteResourcePath = true;

// Force extra "<name>_<hash>" entries into the payload. Fill kExtraEntries
// with real 24.0.1 entries (they show up in the log lines produced below on a
// 24.0.1 client) and flip this to true to make the old client fetch them.
constexpr bool kAppendExtraEntries = false;

constexpr const char* kFromToken = "23.1.3";
constexpr const char* kToToken = "24.0.1";

constexpr const char* kExtraEntries[] = {nullptr};
constexpr const char* kExtraSeparator = "\n";

// Guard rails.
constexpr int32_t kMaxPayloadChars = 400000;
constexpr size_t kPayloadLogChars = 400u;
constexpr int kLogBurst = 24;
constexpr int kLogPeriod = 32;

// Self-bootstrap pacing.
constexpr const char* kIl2Cpp = "libil2cpp.so";
constexpr int kBootSteps = 6000;
constexpr useconds_t kBootStepUs = 10 * 1000;
constexpr int kInstallAttempts = 600;
constexpr useconds_t kInstallRetryUs = 100 * 1000;

// --------------------------------------------------------------- metadata ---

constexpr const char* kNs = "PGCompany.AssetBundles_v3";

// Bundle url/address helper, TypeDefIndex 13786.
constexpr const char* kUrlClass =
    "\u4E06\u4E0E\u4E0F\u4E19\u4E10\u4E14\u4E08\u4E11\u4E07";
// ConfigId picker, 0x1D25128, 0 args.
constexpr const char* kConfigIdMethod =
    "\u4E00\u4E14\u4E0A\u4E1A\u4E0F\u4E04\u4E18\u4E1F\u4E05";
// string -> List<name+hash>, 0x1D36CDC, 1 arg.
constexpr const char* kParsePayloadMethod =
    "\u4E0B\u4E08\u4E12\u4E01\u4E08\u4E0F\u4E05\u4E08\u4E08";
// name+hash -> Uri, 0x1D35758, 1 arg.
constexpr const char* kUriForMethod =
    "\u4E1B\u4E16\u4E09\u4E17\u4E17\u4E08\u4E04\u4E00\u4E0D";
// (platform, string) -> string, 0x1D39130, 2 args.
constexpr const char* kResourcePathMethod =
    "\u4E1D\u4E07\u4E0C\u4E03\u4E0B\u4E14\u4E0F\u4E17\u4E04";

// Bundle name+hash holder.
constexpr const char* kNameAndHashClass =
    "\u4E13\u4E18\u4E0C\u4E08\u4E0F\u4E15\u4E05\u4E18\u4E0D";
// name getter, 0x1D2A674.
constexpr const char* kNameGetter =
    "\u4E17\u4E1D\u4E0E\u4E14\u4E11\u4E17\u4E0C\u4E01\u4E08";
// hash getter, 0x1D2A728.
constexpr const char* kHashGetter =
    "\u4E14\u4E0B\u4E0E\u4E00\u4E1F\u4E15\u4E0A\u4E1B\u4E11";

// AndroidNativeAssetBundleDownloader, TypeDefIndex 13759.
constexpr const char* kAndroidDownloaderClass =
    "\u4E11\u4E1D\u4E15\u4E0A\u4E10\u4E0F\u4E07\u4E11\u4E12";
constexpr const char* kSetLoadingQueueMethod = "SetLoadingQueue";

// ------------------------------------------------------------------ state ---

using ConfigIdFn = int32_t (*)(void*);
using ParsePayloadFn = void* (*)(void*, void*);
using UriForFn = void* (*)(void*, void*);
using ResourcePathFn = void* (*)(int32_t, void*, void*);
using SetLoadingQueueFn = void (*)(void*, void*, void*);
using GetterFn = void* (*)(void*, void*);

ConfigIdFn g_orig_config_id = nullptr;
ParsePayloadFn g_orig_parse_payload = nullptr;
UriForFn g_orig_uri_for = nullptr;
ResourcePathFn g_orig_resource_path = nullptr;
SetLoadingQueueFn g_orig_set_queue = nullptr;

GetterFn g_name_getter = nullptr;
void* g_name_getter_mi = nullptr;
GetterFn g_hash_getter = nullptr;
void* g_hash_getter_mi = nullptr;
GetterFn g_uri_to_string = nullptr;
void* g_uri_to_string_mi = nullptr;

bool g_hooked_config_id = false;
bool g_hooked_parse_payload = false;
bool g_hooked_uri_for = false;
bool g_hooked_resource_path = false;
bool g_hooked_set_queue = false;

int g_uri_calls = 0;
int g_path_calls = 0;
int g_retargeted_paths = 0;
int g_retargeted_payloads = 0;
int32_t g_last_config_id = -1;
bool g_payload_logged = false;

// ---------------------------------------------------------------- helpers ---

bool should_log(int calls) {
    return calls < kLogBurst || (calls % kLogPeriod) == 0;
}

// Reads a managed string without truncation. ascii_exact reports whether the
// UTF-8 byte length matches the UTF-16 code-unit count, which is the condition
// under which byte-level rewriting is safe.
bool read_managed_string(void* managed, std::string* out, bool* ascii_exact) {
    if (managed == nullptr || out == nullptr) return false;
    const int32_t len = il2cpp::string_length(managed);
    if (len < 0 || len > kMaxPayloadChars) return false;
    *out = il2cpp::to_utf8(managed, static_cast<size_t>(len) + 1u);
    if (ascii_exact != nullptr) {
        *ascii_exact = (static_cast<int32_t>(out->size()) == len);
    }
    return true;
}

size_t replace_all(std::string* text, const char* from, const char* to) {
    if (text == nullptr || from == nullptr || to == nullptr) return 0u;
    const size_t from_len = std::strlen(from);
    const size_t to_len = std::strlen(to);
    if (from_len == 0u) return 0u;
    size_t hits = 0u;
    size_t at = text->find(from, 0u);
    while (at != std::string::npos) {
        text->replace(at, from_len, to);
        ++hits;
        at = text->find(from, at + to_len);
    }
    return hits;
}

int32_t list_count(void* list) {
    if (list == nullptr) return -1;
    auto* klass = il2cpp::object_get_class(list);
    if (klass == nullptr) return -1;
    auto* field = il2cpp::class_get_field_from_name(klass, "_size");
    if (field == nullptr) return -1;
    int32_t size = -1;
    il2cpp::field_get_value(list, field, &size);
    return size;
}

std::string call_string_getter(GetterFn fn, void* method, void* self) {
    if (fn == nullptr || self == nullptr) return std::string();
    void* managed = fn(self, method);
    std::string out;
    bool ascii = false;
    if (!read_managed_string(managed, &out, &ascii)) return std::string();
    return out;
}

void resolve_helpers() {
    if (g_name_getter == nullptr) {
        auto* mi = il2cpp::find_method_info(kNs, kNameAndHashClass, kNameGetter, 0);
        if (mi != nullptr) {
            g_name_getter_mi = (void*)mi;
            g_name_getter = reinterpret_cast<GetterFn>(il2cpp::method_pointer(mi));
        }
    }
    if (g_hash_getter == nullptr) {
        auto* mi = il2cpp::find_method_info(kNs, kNameAndHashClass, kHashGetter, 0);
        if (mi != nullptr) {
            g_hash_getter_mi = (void*)mi;
            g_hash_getter = reinterpret_cast<GetterFn>(il2cpp::method_pointer(mi));
        }
    }
    if (g_uri_to_string == nullptr) {
        auto* mi = il2cpp::find_method_info("System", "Uri", "ToString", 0);
        if (mi != nullptr) {
            g_uri_to_string_mi = (void*)mi;
            g_uri_to_string = reinterpret_cast<GetterFn>(il2cpp::method_pointer(mi));
        }
    }
}

// ------------------------------------------------------------------ hooks ---

int32_t hook_config_id(void* method) {
    const int32_t id = (g_orig_config_id != nullptr) ? g_orig_config_id(method) : 0;
    if (id != g_last_config_id) {
        g_last_config_id = id;
        LOGI("%s: the client asks for bundle ConfigId %d (152=iOS, 153=Android)",
             kTag, id);
    }
    return id;
}

void* hook_parse_payload(void* payload, void* method) {
    void* used = payload;
    std::string text;
    bool ascii = false;

    if (read_managed_string(payload, &text, &ascii)) {
        if (!g_payload_logged) {
            g_payload_logged = true;
            const std::string head = text.substr(0u, kPayloadLogChars);
            LOGI("%s: bundle config payload: %zu chars, ascii=%d, head: %s",
                 kTag, text.size(), ascii ? 1 : 0, head.c_str());
        }

        if (kEnabled && kRewriteConfigPayload && ascii) {
            const int32_t before = il2cpp::string_length(payload);
            std::string patched = text;
            const size_t hits = replace_all(&patched, kFromToken, kToToken);
            size_t appended = 0u;
            if (kAppendExtraEntries) {
                for (const char* entry : kExtraEntries) {
                    if (entry == nullptr || entry[0] == '\0') continue;
                    patched.append(kExtraSeparator);
                    patched.append(entry);
                    ++appended;
                }
            }
            if (hits > 0u || appended > 0u) {
                void* rewritten = il2cpp::string_new(patched.c_str());
                const int32_t after =
                    (rewritten != nullptr) ? il2cpp::string_length(rewritten) : -1;
                const bool length_ok =
                    (appended > 0u) ? (after == static_cast<int32_t>(patched.size()))
                                    : (after == before);
                if (rewritten != nullptr && length_ok) {
                    used = rewritten;
                    ++g_retargeted_payloads;
                    LOGI("%s: payload retargeted: %zu '%s'->'%s' swap(s), "
                         "%zu appended entry(ies)",
                         kTag, hits, kFromToken, kToToken, appended);
                } else {
                    LOGW("%s: payload rewrite rejected (len %d -> %d); "
                         "shipping the original bundle list",
                         kTag, before, after);
                }
            } else {
                LOGI("%s: payload carries no '%s' token, so the 23.1.3 bundle "
                     "list is used as-is; paste real 24.0.1 entries into "
                     "kExtraEntries to force them",
                     kTag, kFromToken);
            }
        }
    }

    void* list = (g_orig_parse_payload != nullptr)
                     ? g_orig_parse_payload(used, method)
                     : nullptr;
    LOGI("%s: parsed bundle list -> %d entry(ies)", kTag, list_count(list));
    return list;
}

void* hook_uri_for(void* name_and_hash, void* method) {
    void* uri = (g_orig_uri_for != nullptr) ? g_orig_uri_for(name_and_hash, method)
                                            : nullptr;
    if (should_log(g_uri_calls)) {
        const std::string name =
            call_string_getter(g_name_getter, g_name_getter_mi, name_and_hash);
        const std::string hash =
            call_string_getter(g_hash_getter, g_hash_getter_mi, name_and_hash);
        const std::string url =
            call_string_getter(g_uri_to_string, g_uri_to_string_mi, uri);
        LOGI("%s: bundle '%s' hash '%s' -> %s", kTag, name.c_str(), hash.c_str(),
             url.empty() ? "<url unavailable>" : url.c_str());
    }
    ++g_uri_calls;
    return uri;
}

void* hook_resource_path(int32_t platform, void* argument, void* method) {
    void* result = (g_orig_resource_path != nullptr)
                       ? g_orig_resource_path(platform, argument, method)
                       : nullptr;

    std::string text;
    bool ascii = false;
    const bool readable = read_managed_string(result, &text, &ascii);

    if (kEnabled && kRewriteResourcePath && readable && ascii) {
        std::string patched = text;
        if (replace_all(&patched, kFromToken, kToToken) > 0u) {
            void* rewritten = il2cpp::string_new(patched.c_str());
            if (rewritten != nullptr &&
                il2cpp::string_length(rewritten) == il2cpp::string_length(result)) {
                ++g_retargeted_paths;
                if (should_log(g_path_calls)) {
                    LOGI("%s: resource path '%s' -> '%s' (platform %d)", kTag,
                         text.c_str(), patched.c_str(), platform);
                }
                ++g_path_calls;
                return rewritten;
            }
            LOGW("%s: resource path rewrite rejected for '%s'", kTag, text.c_str());
        }
    }

    if (readable && should_log(g_path_calls)) {
        LOGI("%s: resource path '%s' (platform %d, untouched)", kTag,
             text.c_str(), platform);
    }
    ++g_path_calls;
    return result;
}

void hook_set_loading_queue(void* self, void* queue, void* method) {
    LOGI("%s: Downloader queue armed with %d entry(ies) "
         "(%d url(s) composed, %d payload(s) and %d path(s) retargeted)",
         kTag, list_count(queue), g_uri_calls, g_retargeted_payloads,
         g_retargeted_paths);
    if (g_orig_set_queue != nullptr) g_orig_set_queue(self, queue, method);
}

bool install_once(bool* flag, const hook::ManagedMethod& target,
                  void* replacement, void** original) {
    if (flag == nullptr) return false;
    if (*flag) return true;
    *flag = hook::install(target, replacement, original, false);
    return *flag;
}

// ------------------------------------------------------------- bootstrap ----

void* boot_thread(void*) {
    uintptr_t base = 0u;
    bool found = false;
    for (int i = 0; i < kBootSteps && !found; ++i) {
        found = elfsym::find_library(kIl2Cpp, &base);
        if (!found) usleep(kBootStepUs);
    }
    if (!found) {
        LOGE("%s: %s never appeared; experiment not armed", kTag, kIl2Cpp);
        return nullptr;
    }

    bool resolved = false;
    for (int i = 0; i < kBootSteps && !resolved; ++i) {
        resolved = il2cpp::resolve();
        if (!resolved) usleep(kBootStepUs);
    }
    if (!resolved) {
        LOGE("%s: il2cpp exports were not resolved; experiment not armed", kTag);
        return nullptr;
    }

    // 23.1.3 crashes if il2cpp_domain_get() is polled before runtime init, so
    // reuse the port's validated root-domain publication slot.
    void* domain =
        il2cpp_runtime_2313::wait_for_domain(base, kBootSteps, kBootStepUs);
    if (domain == nullptr) {
        LOGE("%s: root domain never became safe; experiment not armed", kTag);
        return nullptr;
    }

    void* image = nullptr;
    for (int i = 0; i < kBootSteps && image == nullptr; ++i) {
        image = il2cpp::find_image("Assembly-CSharp.dll");
        if (image == nullptr) usleep(kBootStepUs);
    }
    if (image == nullptr) {
        LOGE("%s: Assembly-CSharp.dll never appeared; experiment not armed", kTag);
        return nullptr;
    }

    void* attached = il2cpp::thread_attach(domain);
    bool armed = false;
    for (int i = 0; i < kInstallAttempts && !armed; ++i) {
        armed = install_hooks();
        if (!armed) usleep(kInstallRetryUs);
    }
    if (il2cpp::thread_detach != nullptr && attached != nullptr) {
        il2cpp::thread_detach(attached);
    }

    if (!armed) {
        LOGE("%s: asset-bundle metadata never matched this build; "
             "nothing was patched",
             kTag);
    }
    return nullptr;
}

__attribute__((constructor)) void res_2401_experiment_on_load() {
    pthread_t thread;
    if (pthread_create(&thread, nullptr, &boot_thread, nullptr) == 0) {
        pthread_detach(thread);
    } else {
        LOGE("%s: pthread_create failed; experiment not armed", kTag);
    }
}

} // namespace

bool install_hooks() {
    resolve_helpers();

    install_once(&g_hooked_config_id,
                 hook::ManagedMethod{kNs, kUrlClass, kConfigIdMethod, 0},
                 reinterpret_cast<void*>(&hook_config_id),
                 reinterpret_cast<void**>(&g_orig_config_id));

    const bool payload = install_once(
        &g_hooked_parse_payload,
        hook::ManagedMethod{kNs, kUrlClass, kParsePayloadMethod, 1},
        reinterpret_cast<void*>(&hook_parse_payload),
        reinterpret_cast<void**>(&g_orig_parse_payload));

    const bool uri =
        install_once(&g_hooked_uri_for,
                     hook::ManagedMethod{kNs, kUrlClass, kUriForMethod, 1},
                     reinterpret_cast<void*>(&hook_uri_for),
                     reinterpret_cast<void**>(&g_orig_uri_for));

    install_once(&g_hooked_resource_path,
                 hook::ManagedMethod{kNs, kUrlClass, kResourcePathMethod, 2},
                 reinterpret_cast<void*>(&hook_resource_path),
                 reinterpret_cast<void**>(&g_orig_resource_path));

    install_once(&g_hooked_set_queue,
                 hook::ManagedMethod{kNs, kAndroidDownloaderClass,
                                     kSetLoadingQueueMethod, 1},
                 reinterpret_cast<void*>(&hook_set_loading_queue),
                 reinterpret_cast<void**>(&g_orig_set_queue));

    const bool armed = payload && uri;
    if (armed) {
        LOGI("%s: armed - enabled=%d payload-rewrite=%d path-rewrite=%d "
             "append=%d token '%s'->'%s' (config-id=%d resource-path=%d "
             "queue=%d); UpdatesChecker is untouched",
             kTag, kEnabled ? 1 : 0, kRewriteConfigPayload ? 1 : 0,
             kRewriteResourcePath ? 1 : 0, kAppendExtraEntries ? 1 : 0,
             kFromToken, kToToken, g_hooked_config_id ? 1 : 0,
             g_hooked_resource_path ? 1 : 0, g_hooked_set_queue ? 1 : 0);
    }
    return armed;
}

} // namespace res_2401_experiment_2313
