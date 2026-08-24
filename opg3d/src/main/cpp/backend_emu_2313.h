#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) local backend - transport interception
//
// Every request the game makes to a retired service is redirected to the
// emulated backend that backend_emu_http.h brings up inside this library. The
// redirect happens at the only two transports this build actually uses,
// UnityEngine.Networking.UnityWebRequest and UnityEngine.WWW, by rewriting the
// URL string before the original method ever sees it:
//
//   https://server-v2.pixelgun3dserver.com/auth_v2/?id_player=1
//     -> http://127.0.0.1:47317/@server-v2.pixelgun3dserver.com/auth_v2/?id_player=1
//
// Why the URL and not the response
// --------------------------------
// Synthesising responses would mean hooking every result member of every
// transport (isDone, downloadHandler.text, error, responseCode, ...) and
// keeping their states consistent. Rewriting the address instead keeps the
// game on its own real HTTP path: status codes, POST bodies, headers, retries
// and coroutine timing all behave exactly as they did against the live
// service, and the only thing that changed is which socket answers.
//
// It also removes TLS from the picture. The rewritten address is plain http on
// a private network, so no certificate has to be trusted, pinned or faked.
//
// Overload safety
// ---------------
// UnityWebRequest overloads its constructors and helpers on (string) and
// (Uri), and IL2CPP metadata lookup by name plus argument count cannot tell
// those apart. So no argument is ever assumed to be a string: every candidate
// is type-checked against System.String first, and anything else is passed
// through untouched.
// -----------------------------------------------------------------------------

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "backend_emu_http.h"
#include "backend_emu_routes.h"
#include "backend_emu_store.h"
#include "hook.h"
#include "il2cpp.h"
#include "log.h"

namespace backend_emu_2313 {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ----------------------------------------------------------- metadata names

constexpr const char* kNetworkingNs = "UnityEngine.Networking";
constexpr const char* kUnityNs = "UnityEngine";
constexpr const char* kWebRequest = "UnityWebRequest";
constexpr const char* kWww = "WWW";

// Host suffixes of the retired services, taken from the string literals in
// global-metadata.dat. Photon's own hosts are deliberately absent: the Photon
// port owns that traffic and it is not HTTP.
constexpr const char* kBackendHosts[] = {
    "pixelgun3dserver.com",
    "pixelgun3d.com",
    "pixelgunserver.com",
    "lightmap.com",
};

constexpr uint64_t kLogBurst = 16u;
constexpr uint64_t kLogPeriod = 64u;
constexpr size_t kMaxUrlLength = 4096u;

// ------------------------------------------------------------- managed ABI
//
// Generated managed methods take their explicit arguments followed by
// MethodInfo*. Instance methods take `this` first.

using Ctor2Fn = void (*)(void* self, void* a, void* b, void* method);
using Ctor4Fn = void (*)(void* self, void* a, void* b, void* c, void* d,
                         void* method);
using SetUrlFn = void (*)(void* self, void* value, void* method);
using Static1Fn = void* (*)(void* a, void* method);
using Static2Fn = void* (*)(void* a, void* b, void* method);
using WwwCtor1Fn = void (*)(void* self, void* a, void* method);
using WwwCtor2Fn = void (*)(void* self, void* a, void* b, void* method);
using WwwCtor3Fn = void (*)(void* self, void* a, void* b, void* c,
                            void* method);

inline Ctor2Fn g_orig_uwr_ctor2 = nullptr;
inline Ctor4Fn g_orig_uwr_ctor4 = nullptr;
inline SetUrlFn g_orig_uwr_set_url = nullptr;
inline Static1Fn g_orig_uwr_get = nullptr;
inline Static2Fn g_orig_uwr_post = nullptr;
inline WwwCtor1Fn g_orig_www_ctor1 = nullptr;
inline WwwCtor2Fn g_orig_www_ctor2 = nullptr;
inline WwwCtor3Fn g_orig_www_ctor3 = nullptr;

// ------------------------------------------------------------------- state

inline bool g_installed = false;
inline uint64_t g_rewrites = 0u;
inline uint64_t g_passthrough = 0u;
inline uint64_t g_non_string = 0u;

inline bool should_log(uint64_t counter) {
    return counter <= kLogBurst || (counter % kLogPeriod) == 0u;
}

// -------------------------------------------------------- managed strings

// Only a real System.String may be read as one: the (Uri) overloads share the
// name and argument count of the (string) ones in metadata.
inline bool is_managed_string(void* candidate) {
    if (candidate == nullptr) return false;
    if (il2cpp::object_get_class == nullptr || il2cpp::class_get_name == nullptr) {
        return false;
    }
    void* klass = il2cpp::object_get_class(candidate);
    if (klass == nullptr) return false;
    const char* name = il2cpp::class_get_name(klass);
    return name != nullptr && std::strcmp(name, "String") == 0;
}

inline bool starts_with_ci(const std::string& text, const char* prefix) {
    const size_t length = std::strlen(prefix);
    if (text.size() < length) return false;
    for (size_t i = 0u; i < length; ++i) {
        char left = text[i];
        char right = prefix[i];
        if (left >= 'A' && left <= 'Z') left = static_cast<char>(left + 32);
        if (right >= 'A' && right <= 'Z') right = static_cast<char>(right + 32);
        if (left != right) return false;
    }
    return true;
}

inline bool ends_with(const std::string& text, const char* suffix) {
    const size_t length = std::strlen(suffix);
    if (text.size() < length) return false;
    return text.compare(text.size() - length, length, suffix) == 0;
}

// --------------------------------------------------------------- rewriting

// Splits an absolute http(s) URL and rebuilds it against the emulated
// backend, keeping the original host as the first path segment so the handlers
// can tell the services apart.
inline bool rewrite_url(const std::string& url, std::string* out) {
    if (url.size() > kMaxUrlLength) return false;

    size_t host_start = 0u;
    if (starts_with_ci(url, "https://")) {
        host_start = 8u;
    } else if (starts_with_ci(url, "http://")) {
        host_start = 7u;
    } else {
        return false;
    }

    size_t host_end = url.find('/', host_start);
    const size_t authority_end =
        host_end == std::string::npos ? url.size() : host_end;
    std::string authority = url.substr(host_start, authority_end - host_start);
    const std::string tail =
        host_end == std::string::npos ? std::string("/") : url.substr(host_end);

    // Drop credentials and the port, keep the bare host for matching.
    const size_t at = authority.find('@');
    if (at != std::string::npos) authority = authority.substr(at + 1u);
    std::string host = authority;
    const size_t colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0u, colon);
    if (host.empty()) return false;

    // Never touch what already points at the emulated backend, and never
    // touch a private address: those are the LAN host and Photon.
    if (host == "127.0.0.1" || host == "localhost" ||
        starts_with_ci(host, "192.168.") || starts_with_ci(host, "10.")) {
        return false;
    }

    bool known = false;
    for (const char* suffix : kBackendHosts) {
        if (ends_with(host, suffix)) {
            known = true;
            break;
        }
    }
    if (!known) return false;

    std::string rebuilt("http://");
    rebuilt += backend_emu_http::endpoint();
    rebuilt += "/@";
    rebuilt += host;
    if (!tail.empty() && tail[0] != '/') rebuilt += '/';
    rebuilt += tail;
    if (out != nullptr) *out = rebuilt;
    return true;
}

// Returns a replacement managed string, or nullptr when the argument has to be
// passed through unchanged.
inline void* redirect(void* candidate, const char* surface) {
    if (candidate == nullptr) return nullptr;
    if (!backend_emu_http::ready()) return nullptr;
    if (!is_managed_string(candidate)) {
        ++g_non_string;
        if (should_log(g_non_string)) {
            LOGI("23.1.3-backend-emu: %s was called with a non-string address"
                 " (probably the Uri overload); it was passed through"
                 " (#%" PRIu64 ")", surface, g_non_string);
        }
        return nullptr;
    }

    const std::string url = il2cpp::to_utf8(candidate, kMaxUrlLength);
    std::string rewritten;
    if (!rewrite_url(url, &rewritten)) {
        ++g_passthrough;
        return nullptr;
    }
    if (il2cpp::string_new == nullptr) return nullptr;
    void* managed = il2cpp::string_new(rewritten.c_str());
    if (managed == nullptr) return nullptr;

    ++g_rewrites;
    if (should_log(g_rewrites)) {
        LOGI("23.1.3-backend-emu: %s -> local backend (#%" PRIu64 "): %s",
             surface, g_rewrites, url.c_str());
    }
    return managed;
}

// -------------------------------------------------------------------- hooks

inline void uwr_ctor2_hook(void* self, void* url, void* method_name,
                           void* method) {
    void* replacement = redirect(url, "UnityWebRequest(url, method)");
    if (g_orig_uwr_ctor2 != nullptr) {
        g_orig_uwr_ctor2(self, replacement != nullptr ? replacement : url,
                         method_name, method);
    }
}

inline void uwr_ctor4_hook(void* self, void* url, void* method_name,
                           void* download, void* upload, void* method) {
    void* replacement = redirect(url, "UnityWebRequest(url, method, dl, ul)");
    if (g_orig_uwr_ctor4 != nullptr) {
        g_orig_uwr_ctor4(self, replacement != nullptr ? replacement : url,
                         method_name, download, upload, method);
    }
}

inline void uwr_set_url_hook(void* self, void* value, void* method) {
    void* replacement = redirect(value, "UnityWebRequest.url setter");
    if (g_orig_uwr_set_url != nullptr) {
        g_orig_uwr_set_url(self, replacement != nullptr ? replacement : value,
                           method);
    }
}

inline void* uwr_get_hook(void* uri, void* method) {
    void* replacement = redirect(uri, "UnityWebRequest.Get");
    return g_orig_uwr_get != nullptr
               ? g_orig_uwr_get(replacement != nullptr ? replacement : uri, method)
               : nullptr;
}

inline void* uwr_post_hook(void* uri, void* form, void* method) {
    void* replacement = redirect(uri, "UnityWebRequest.Post");
    return g_orig_uwr_post != nullptr
               ? g_orig_uwr_post(replacement != nullptr ? replacement : uri,
                                 form, method)
               : nullptr;
}

inline void www_ctor1_hook(void* self, void* url, void* method) {
    void* replacement = redirect(url, "WWW(url)");
    if (g_orig_www_ctor1 != nullptr) {
        g_orig_www_ctor1(self, replacement != nullptr ? replacement : url, method);
    }
}

inline void www_ctor2_hook(void* self, void* url, void* form, void* method) {
    void* replacement = redirect(url, "WWW(url, form)");
    if (g_orig_www_ctor2 != nullptr) {
        g_orig_www_ctor2(self, replacement != nullptr ? replacement : url, form,
                         method);
    }
}

inline void www_ctor3_hook(void* self, void* url, void* data, void* headers,
                           void* method) {
    void* replacement = redirect(url, "WWW(url, data, headers)");
    if (g_orig_www_ctor3 != nullptr) {
        g_orig_www_ctor3(self, replacement != nullptr ? replacement : url, data,
                         headers, method);
    }
}

// ------------------------------------------------------------- installation

inline bool install() {
    if (g_installed) return true;

    // Minting the id before anything else means the very first log line of the
    // session already names the account the game will run as.
    const char* id = backend_emu_store::player_id();

    backend_emu_routes::install();
    if (!backend_emu_http::start()) {
        LOGE("23.1.3-backend-emu: the local backend did not come up; no URL was"
             " redirected and the game keeps talking to the retired services");
        return false;
    }

    int webrequest_hooks = 0;
    if (hook::install({kNetworkingNs, kWebRequest, ".ctor", 2},
                      reinterpret_cast<void*>(&uwr_ctor2_hook),
                      reinterpret_cast<void**>(&g_orig_uwr_ctor2))) {
        ++webrequest_hooks;
    }
    if (hook::install({kNetworkingNs, kWebRequest, ".ctor", 4},
                      reinterpret_cast<void*>(&uwr_ctor4_hook),
                      reinterpret_cast<void**>(&g_orig_uwr_ctor4))) {
        ++webrequest_hooks;
    }
    if (hook::install({kNetworkingNs, kWebRequest, "set_url", 1},
                      reinterpret_cast<void*>(&uwr_set_url_hook),
                      reinterpret_cast<void**>(&g_orig_uwr_set_url))) {
        ++webrequest_hooks;
    }
    if (hook::install({kNetworkingNs, kWebRequest, "Get", 1},
                      reinterpret_cast<void*>(&uwr_get_hook),
                      reinterpret_cast<void**>(&g_orig_uwr_get))) {
        ++webrequest_hooks;
    }
    if (hook::install({kNetworkingNs, kWebRequest, "Post", 2},
                      reinterpret_cast<void*>(&uwr_post_hook),
                      reinterpret_cast<void**>(&g_orig_uwr_post))) {
        ++webrequest_hooks;
    }

    int www_hooks = 0;
    if (hook::install({kUnityNs, kWww, ".ctor", 1},
                      reinterpret_cast<void*>(&www_ctor1_hook),
                      reinterpret_cast<void**>(&g_orig_www_ctor1))) {
        ++www_hooks;
    }
    if (hook::install({kUnityNs, kWww, ".ctor", 2},
                      reinterpret_cast<void*>(&www_ctor2_hook),
                      reinterpret_cast<void**>(&g_orig_www_ctor2))) {
        ++www_hooks;
    }
    if (hook::install({kUnityNs, kWww, ".ctor", 3},
                      reinterpret_cast<void*>(&www_ctor3_hook),
                      reinterpret_cast<void**>(&g_orig_www_ctor3))) {
        ++www_hooks;
    }

    if (webrequest_hooks == 0 && www_hooks == 0) {
        LOGE("23.1.3-backend-emu: neither UnityWebRequest nor WWW could be"
             " hooked; the local backend is listening on %s but the game's"
             " traffic still leaves the device",
             backend_emu_http::endpoint());
        return false;
    }

    g_installed = true;
    LOGI("23.1.3-backend-emu: armed as %s at %s - id_player=%s, %d/5"
         " UnityWebRequest and %d/3 WWW entry point(s) redirected; the retired"
         " account, config, telemetry, leaderboard and clan services are now"
         " answered on this device",
         backend_emu_http::is_host() ? "LAN host" : "LAN client",
         backend_emu_http::endpoint(), id, webrequest_hooks, www_hooks);
    return true;
}

}  // namespace detail

// Brings up the emulated backend and redirects the game's HTTP transports to
// it. Must run after the IL2CPP metadata is available.
inline bool install_hooks() { return detail::install(); }

}  // namespace backend_emu_2313
