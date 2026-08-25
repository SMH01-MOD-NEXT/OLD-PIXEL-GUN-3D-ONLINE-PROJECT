#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) local backend - persistent state
//
// The emulated backend keeps its own state next to the game's own data, in
// <app data>/files/opg3d-backend/state.kv. Nothing here touches PlayerPrefs:
// this build encrypts every pref through CryptoPlayerPrefsManager (salt +
// Rijndael + XOR), so the plaintext key of a pref never exists at runtime and
// hooking PlayerPrefs by key name can never match.
//
// Who owns the account id
// -----------------------
// The game does. Because the pref that holds the id is encrypted, this port
// can neither read nor overwrite it, so the id the player sees on screen is
// always the id the game itself keeps. The game does however send that id with
// every authentication call (auth_v2/?id_player=...), so the local backend
// adopts what the game presents and stores it here. An id is minted locally
// only when the game presents none at all, which is what a genuinely fresh
// install looks like, and such an id is provisional: the first authentication
// that names a real account replaces it.
//
// The previous revision minted an id unconditionally and the auth route
// answered with it while ignoring the request, so the game kept its own id and
// state.kv held a different one. That is the reported "the game shows 35...,
// the file holds something else" mismatch, and it could never converge on its
// own.
//
// Format: one "key\tvalue" line per entry, with \\, \n, \r and \t escaped, so
// JSON blobs can be stored verbatim as values. Writes go through a temporary
// file and rename(), so a kill during a save can never truncate the state.
// -----------------------------------------------------------------------------

#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>

#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"

namespace backend_emu_store {
namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

constexpr const char* kDirName = "opg3d-backend";
constexpr const char* kFileName = "state.kv";
constexpr size_t kMaxStateBytes = 8u * 1024u * 1024u;

// Minting shape: exactly nine digits, never a leading zero - the shape the old
// account service handed out to a brand new account.
constexpr uint32_t kIdFirst = 100000000u;
constexpr uint32_t kIdSpan = 900000000u;

// Accepting shape: any plausible account id, because an id that comes from the
// game is not necessarily nine digits wide. The ids this build carries are ten
// digits ("35..."), and a nine-digit-only check silently rejected them.
constexpr size_t kIdMinDigits = 5u;
constexpr size_t kIdMaxDigits = 18u;

inline pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
inline std::map<std::string, std::string> g_values;
inline std::string g_dir;
inline std::string g_file;
inline bool g_loaded = false;
inline bool g_persistent = false;
inline char g_id[24] = {};
inline char g_session[33] = {};

struct Guard {
    Guard() { pthread_mutex_lock(&g_lock); }
    ~Guard() { pthread_mutex_unlock(&g_lock); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

// ------------------------------------------------------------------ encoding

inline std::string escape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 8u);
    for (const char c : raw) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

inline std::string unescape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0u; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1u >= raw.size()) {
            out += raw[i];
            continue;
        }
        ++i;
        switch (raw[i]) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case '\\': out += '\\'; break;
            default: out += raw[i]; break;
        }
    }
    return out;
}

// -------------------------------------------------------------- data folder

// "com.pixel.gun3d" or "com.pixel.gun3d:sub" -> "com.pixel.gun3d".
inline std::string process_name() {
    const int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return std::string();
    char buffer[256];
    const ssize_t got = read(fd, buffer, sizeof(buffer) - 1u);
    close(fd);
    if (got <= 0) return std::string();
    buffer[got] = '\0';
    char* colon = std::strchr(buffer, ':');
    if (colon != nullptr) *colon = '\0';
    return std::string(buffer);
}

inline bool ensure_dir(const std::string& path) {
    if (path.empty()) return false;
    if (mkdir(path.c_str(), 0770) == 0) return true;
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

inline bool try_root(const std::string& root, bool* persistent) {
    struct stat info {};
    if (root.empty() || stat(root.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        return false;
    }
    const std::string candidate = root + "/" + kDirName;
    if (!ensure_dir(candidate)) return false;
    if (access(candidate.c_str(), R_OK | W_OK | X_OK) != 0) return false;
    g_dir = candidate;
    g_file = candidate + "/" + kFileName;
    *persistent = true;
    return true;
}

// The app's private files directory is the only location guaranteed to be
// writable without any permission, on every Android version this build runs
// on. /data/local/tmp is a development fallback only.
inline void resolve_dir_locked() {
    if (!g_dir.empty()) return;
    const std::string package = process_name();
    bool persistent = false;
    if (!package.empty()) {
        if (try_root("/data/user/0/" + package + "/files", &persistent) ||
            try_root("/data/data/" + package + "/files", &persistent) ||
            try_root("/data/user/0/" + package, &persistent) ||
            try_root("/data/data/" + package, &persistent)) {
            g_persistent = persistent;
            LOGI("23.1.3-backend-store: state directory '%s'", g_dir.c_str());
            return;
        }
    }
    if (try_root("/data/local/tmp", &persistent)) {
        g_persistent = persistent;
        LOGW("23.1.3-backend-store: private app storage was unreachable;"
             " falling back to '%s'", g_dir.c_str());
        return;
    }
    g_persistent = false;
    LOGE("23.1.3-backend-store: no writable directory was found; the emulated"
         " backend runs with in-memory state only (a relaunch starts fresh)");
}

// ---------------------------------------------------------------- load/save

inline void load_locked() {
    if (g_loaded) return;
    g_loaded = true;
    resolve_dir_locked();
    if (!g_persistent) return;

    const int fd = open(g_file.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOGI("23.1.3-backend-store: no previous state; a new profile will be"
             " created");
        return;
    }
    std::string blob;
    char chunk[4096];
    for (;;) {
        const ssize_t got = read(fd, chunk, sizeof(chunk));
        if (got <= 0) break;
        blob.append(chunk, static_cast<size_t>(got));
        if (blob.size() > kMaxStateBytes) {
            LOGW("23.1.3-backend-store: state file exceeds %zu bytes; the tail"
                 " is ignored", kMaxStateBytes);
            break;
        }
    }
    close(fd);

    size_t entries = 0u;
    size_t start = 0u;
    while (start <= blob.size()) {
        size_t end = blob.find('\n', start);
        if (end == std::string::npos) end = blob.size();
        const std::string line = blob.substr(start, end - start);
        start = end + 1u;
        if (line.empty()) continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        g_values[line.substr(0u, tab)] = unescape(line.substr(tab + 1u));
        ++entries;
    }
    LOGI("23.1.3-backend-store: loaded %zu stored value(s)", entries);
}

inline void save_locked() {
    if (!g_persistent) return;
    const std::string temporary = g_file + ".tmp";
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                       0660);
    if (fd < 0) {
        LOGW("23.1.3-backend-store: could not open '%s' for writing (errno=%d)",
             temporary.c_str(), errno);
        return;
    }
    std::string blob;
    for (const auto& entry : g_values) {
        blob += entry.first;
        blob += '\t';
        blob += escape(entry.second);
        blob += '\n';
    }
    size_t written = 0u;
    bool ok = true;
    while (written < blob.size() && ok) {
        const ssize_t produced =
            write(fd, blob.data() + written, blob.size() - written);
        if (produced <= 0) {
            ok = false;
            break;
        }
        written += static_cast<size_t>(produced);
    }
    fsync(fd);
    close(fd);
    if (!ok) {
        unlink(temporary.c_str());
        LOGW("23.1.3-backend-store: the state write failed; the previous state"
             " is kept");
        return;
    }
    if (rename(temporary.c_str(), g_file.c_str()) != 0) {
        unlink(temporary.c_str());
        LOGW("23.1.3-backend-store: rename to '%s' failed (errno=%d)",
             g_file.c_str(), errno);
    }
}

// ------------------------------------------------------------------ minting

inline uint64_t random64() {
    uint64_t value = 0u;
    const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        const ssize_t got = read(fd, &value, sizeof(value));
        close(fd);
        if (got == static_cast<ssize_t>(sizeof(value)) && value != 0u) return value;
    }
    timespec now {};
    clock_gettime(CLOCK_REALTIME, &now);
    value = static_cast<uint64_t>(now.tv_sec) * 1000000000ull +
            static_cast<uint64_t>(now.tv_nsec);
    value ^= static_cast<uint64_t>(getpid()) << 32;
    return value != 0u ? value : 0x9E3779B97F4A7C15ull;
}

// splitmix64 finalizer: two launches on the same device never produce
// neighbouring ids.
inline uint64_t whiten(uint64_t seed) {
    uint64_t z = seed + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Digits only, no leading zero, plausible width. Deliberately wider than the
// minting shape so an id that arrives from the game is accepted as it is
// rather than replaced by one of ours.
inline bool looks_like_id(const std::string& text) {
    if (text.size() < kIdMinDigits || text.size() > kIdMaxDigits) return false;
    if (text[0] < '1' || text[0] > '9') return false;
    for (size_t i = 1u; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
    }
    return true;
}

// Trims the padding a query string or a form field can carry, then applies the
// shape test. An empty result means "this is not an account id", which is what
// the "?id_player=1" an unregistered client sends comes back as.
inline std::string normalize(const std::string& raw) {
    const auto strip = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' ||
               c == '\'';
    };
    size_t start = 0u;
    size_t end = raw.size();
    while (start < end && strip(raw[start])) ++start;
    while (end > start && strip(raw[end - 1u])) --end;
    const std::string trimmed = raw.substr(start, end - start);
    return looks_like_id(trimmed) ? trimmed : std::string();
}

inline std::string get_locked(const std::string& key, const char* fallback) {
    const auto found = g_values.find(key);
    if (found != g_values.end()) return found->second;
    return fallback != nullptr ? std::string(fallback) : std::string();
}

inline void set_locked(const std::string& key, const std::string& value) {
    const auto found = g_values.find(key);
    if (found != g_values.end() && found->second == value) return;
    g_values[key] = value;
    save_locked();
}

}  // namespace detail

// ---------------------------------------------------------------- public API

// What happened to an id the game presented.
enum class Adoption {
    kRejected,   // not an account id (empty, a placeholder such as "1", ...)
    kUnchanged,  // already the stored id
    kAdopted,    // the stored id was replaced by the presented one
};

inline std::string get(const char* key, const char* fallback = "") {
    if (key == nullptr) return std::string();
    detail::Guard guard;
    detail::load_locked();
    return detail::get_locked(key, fallback);
}

inline bool has(const char* key) {
    if (key == nullptr) return false;
    detail::Guard guard;
    detail::load_locked();
    return detail::g_values.find(key) != detail::g_values.end();
}

inline void set(const char* key, const std::string& value) {
    if (key == nullptr) return;
    detail::Guard guard;
    detail::load_locked();
    detail::set_locked(key, value);
}

inline int64_t get_int(const char* key, int64_t fallback) {
    const std::string text = get(key, "");
    if (text.empty()) return fallback;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str()) return fallback;
    return static_cast<int64_t>(parsed);
}

inline void set_int(const char* key, int64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "%" PRId64, value);
    set(key, text);
}

inline int64_t add_int(const char* key, int64_t delta, int64_t fallback) {
    if (key == nullptr) return fallback;
    detail::Guard guard;
    detail::load_locked();
    const std::string text = detail::get_locked(key, "");
    int64_t current = fallback;
    if (!text.empty()) {
        char* end = nullptr;
        const long long parsed = std::strtoll(text.c_str(), &end, 10);
        if (end != text.c_str()) current = static_cast<int64_t>(parsed);
    }
    current += delta;
    char encoded[24];
    std::snprintf(encoded, sizeof(encoded), "%" PRId64, current);
    detail::set_locked(key, encoded);
    return current;
}

// "" when the text is not an account id, the trimmed id otherwise.
inline std::string normalize_id(const std::string& raw) {
    return detail::normalize(raw);
}

inline bool id_shaped(const std::string& raw) {
    return !detail::normalize(raw).empty();
}

// The account id this device answers for. Adopted from the game on the first
// authentication, reused by every later launch, and regenerated only when the
// player wipes the game data.
inline const char* player_id() {
    if (detail::g_id[0] != '\0') return detail::g_id;
    detail::Guard guard;
    if (detail::g_id[0] != '\0') return detail::g_id;
    detail::load_locked();

    const std::string stored = detail::get_locked("id_player", "");
    if (detail::looks_like_id(stored)) {
        std::snprintf(detail::g_id, sizeof(detail::g_id), "%s", stored.c_str());
        LOGI("23.1.3-backend-store: reusing local account id %s (source '%s')",
             detail::g_id,
             detail::get_locked("id_player_source", "minted").c_str());
        return detail::g_id;
    }
    const uint32_t minted =
        detail::kIdFirst +
        static_cast<uint32_t>(detail::whiten(detail::random64()) % detail::kIdSpan);
    std::snprintf(detail::g_id, sizeof(detail::g_id), "%" PRIu32, minted);
    detail::set_locked("id_player", detail::g_id);
    detail::set_locked("id_player_source", "minted");
    LOGI("23.1.3-backend-store: minted the provisional account id %s; the"
         " first authentication that names a real account replaces it",
         detail::g_id);
    return detail::g_id;
}

// "client" once an id was adopted from the game, "minted" while the id is
// still the provisional one this port generated.
inline std::string player_id_source() {
    const std::string stored = get("id_player_source", "");
    return stored.empty() ? std::string("minted") : stored;
}

// Binds this device's state to the id the game says it is running as.
//
// This is the only direction that can work on this build: the pref that holds
// the game's id is encrypted, so it cannot be read or rewritten from here, and
// an authentication answer naming a different id leaves the game on its own
// id. Following the game instead makes the id on screen and the id in
// state.kv the same account by construction.
inline Adoption adopt_player_id(const std::string& presented) {
    const std::string candidate = detail::normalize(presented);
    if (candidate.empty()) return Adoption::kRejected;

    detail::Guard guard;
    detail::load_locked();
    detail::set_locked("id_player_presented", candidate);

    const std::string current = detail::get_locked("id_player", "");
    if (current == candidate) {
        if (detail::g_id[0] == '\0') {
            std::snprintf(detail::g_id, sizeof(detail::g_id), "%s",
                          candidate.c_str());
        }
        detail::set_locked("id_player_source", "client");
        return Adoption::kUnchanged;
    }

    detail::set_locked("id_player", candidate);
    detail::set_locked("id_player_source", "client");
    if (!current.empty()) detail::set_locked("id_player_previous", current);
    std::snprintf(detail::g_id, sizeof(detail::g_id), "%s", candidate.c_str());

    // A nickname derived from the replaced id would keep naming an account that
    // never existed, so it is dropped and derived again on the next read. A
    // nickname the player chose is left alone.
    if (detail::get_locked("nick_auto", "") == "1") {
        detail::set_locked("nick", "");
    }

    LOGI("23.1.3-backend-store: adopted the account id the game presented"
         " (%s -> %s); state.kv now names the account the game shows",
         current.empty() ? "none" : current.c_str(), candidate.c_str());
    return Adoption::kAdopted;
}

// Session token of this process run. Regenerated per launch, exactly like the
// token the retired service used to hand out.
inline const char* session_token() {
    if (detail::g_session[0] != '\0') return detail::g_session;
    detail::Guard guard;
    if (detail::g_session[0] != '\0') return detail::g_session;
    const uint64_t high = detail::whiten(detail::random64());
    const uint64_t low = detail::whiten(detail::random64() ^ high);
    std::snprintf(detail::g_session, sizeof(detail::g_session),
                  "%016" PRIx64 "%016" PRIx64, high, low);
    return detail::g_session;
}

inline std::string nickname() {
    std::string stored = get("nick", "");
    if (!stored.empty()) return stored;
    const char* id = player_id();
    const size_t length = std::strlen(id);
    const char* tail = length > 4u ? id + (length - 4u) : id;
    stored = std::string("Player") + tail;
    set("nick", stored);
    set("nick_auto", "1");
    return stored;
}

inline uint64_t now_unix() {
    return static_cast<uint64_t>(::time(nullptr));
}

inline void flush() {
    detail::Guard guard;
    detail::load_locked();
    detail::save_locked();
}

inline std::string directory() {
    detail::Guard guard;
    detail::load_locked();
    return detail::g_dir;
}

}  // namespace backend_emu_store
