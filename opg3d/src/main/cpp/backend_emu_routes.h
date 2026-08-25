#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) local backend - endpoints
//
// One handler per retired service, all answered from device-local state. The
// hosts these used to live on are recovered from the /@host/ prefix that the
// rewrite in backend_emu_2313.h adds, so auth, config, stats, leaderboards and
// the clan services are told apart even though a single socket serves them.
//
// Two deliberate choices
// ----------------------
//   * the account id is read out of the authentication request rather than
//     dictated to the game. This build encrypts every pref
//     (CryptoPlayerPrefsManager: salt + Rijndael + XOR), so the id the game
//     runs as can neither be read nor rewritten from here, and an answer that
//     names a different id simply loses: the game keeps its own. The game does
//     send that id with every call (auth_v2/?id_player=...), so the local
//     backend adopts it and binds its own state to it. That is what makes the
//     id on screen and the id in state.kv the same account;
//   * unknown endpoints are answered by a permissive generic payload instead
//     of failing. A miss then costs a warning line in logcat naming the exact
//     host and path, which is what the next iteration needs in order to give
//     that endpoint a real schema, while the game keeps running in the
//     meantime.
//
// Answers carry every spelling of a field this build's parsers are known to
// look for (id_player / player_id / id, session / token, nick / name). A JSON
// consumer ignores the members it does not know, so covering the aliases costs
// nothing and removes a whole class of "which key does it read" guesswork.
// -----------------------------------------------------------------------------

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>

#include "backend_emu_http.h"
#include "backend_emu_store.h"
#include "log.h"

namespace backend_emu_routes {
namespace detail {

using backend_emu_http::Request;
using backend_emu_http::Response;

constexpr uint64_t kLogBurst = 8u;
constexpr uint64_t kLogPeriod = 64u;

inline uint64_t g_auth_without_id = 0u;
inline uint64_t g_auth_lan = 0u;
inline uint64_t g_auth_kept = 0u;

inline bool should_log(uint64_t counter) {
    return counter <= kLogBurst || (counter % kLogPeriod) == 0u;
}

// ---------------------------------------------------------------- JSON tools

inline std::string escape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 8u);
    for (const char c : raw) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20u) {
                    char unicode[8];
                    std::snprintf(unicode, sizeof(unicode), "\\u%04x",
                                  static_cast<unsigned int>(
                                      static_cast<unsigned char>(c)));
                    out += unicode;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

inline std::string text_member(const char* name, const std::string& value) {
    return std::string("\"") + name + "\":\"" + escape(value) + "\"";
}

inline std::string number_member(const char* name, int64_t value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%" PRId64, value);
    return std::string("\"") + name + "\":" + text;
}

inline void answer(Response& response, const std::string& body) {
    response.status = 200;
    response.content_type = "application/json; charset=utf-8";
    response.body = body;
}

// Every answer carries the same success envelope: this build checks a
// different member of it per service, so all of the known spellings are set.
inline std::string envelope() {
    const int64_t now = static_cast<int64_t>(backend_emu_store::now_unix());
    return text_member("result", "ok") + "," + text_member("status", "ok") +
           "," + number_member("error", 0) + "," +
           number_member("error_code", 0) + "," + text_member("message", "") +
           "," + number_member("time", now) + "," +
           number_member("server_time", now) + "," +
           number_member("maintenance", 0) + "," +
           number_member("maintanance", 0);
}

// -------------------------------------------------------------------- clock

// The stock service answered with bare seconds and the parsers in this build
// are int.Parse()-shaped, so plain text is the safe default. A caller that
// asks for JSON explicitly still gets JSON.
inline bool handle_time(const Request& request, Response& response) {
    const int64_t now = static_cast<int64_t>(backend_emu_store::now_unix());
    if (request.query.find("json") != std::string::npos) {
        answer(response, "{" + envelope() + "," + number_member("unixtime", now) +
                             "}");
        return true;
    }
    char text[32];
    std::snprintf(text, sizeof(text), "%" PRId64, now);
    response.status = 200;
    response.content_type = "text/plain; charset=utf-8";
    response.body = text;
    return true;
}

// ------------------------------------------------------------------- account

// Every spelling of the id field the retired services accepted. Request::param
// looks in the query string first and then in the body, so an urlencoded
// WWWForm and a multipart one are both covered.
constexpr const char* kIdAliases[] = {
    "id_player", "player_id", "user_id", "account_id", "id_user", "uid", "id",
};

// The id the game itself is running as, or "" when it presented none. The
// "?id_player=1" an unregistered client sends fails the shape test, so it is
// correctly reported as "no id" instead of being adopted as account 1.
inline std::string presented_id(const Request& request) {
    for (const char* alias : kIdAliases) {
        const std::string raw = request.param(alias, "");
        if (raw.empty()) continue;
        const std::string candidate = backend_emu_store::normalize_id(raw);
        if (!candidate.empty()) return candidate;
    }
    return std::string();
}

// Only this device may rewrite this device's account. On a LAN host the peers
// are other players, and adopting their ids would hand the host's state to
// whoever authenticated last.
inline bool from_this_device(const Request& request) {
    if (request.peer.empty()) return true;
    if (request.peer == "::1") return true;
    return request.peer.compare(0u, 4u, "127.") == 0;
}

// The identity handshake, and the one place the two sides of the account id
// meet: whatever the game presents here becomes the id this backend answers
// for, so the id on screen and the id in state.kv cannot drift apart.
inline bool handle_auth(const Request& request, Response& response) {
    const std::string presented = presented_id(request);
    const bool local = from_this_device(request);

    std::string id;
    if (!presented.empty() && local) {
        const backend_emu_store::Adoption outcome =
            backend_emu_store::adopt_player_id(presented);
        id = backend_emu_store::player_id();
        if (outcome == backend_emu_store::Adoption::kUnchanged) {
            ++g_auth_kept;
            if (should_log(g_auth_kept)) {
                LOGI("23.1.3-backend-auth: the game authenticated as"
                     " id_player=%s, which is already the stored account"
                     " (confirmation #%" PRIu64 ")", id.c_str(), g_auth_kept);
            }
        }
    } else if (!presented.empty()) {
        // A LAN client: answer about its own account and leave ours untouched.
        id = presented;
        ++g_auth_lan;
        if (should_log(g_auth_lan)) {
            LOGI("23.1.3-backend-auth: LAN client %s authenticated as"
                 " id_player=%s; this host keeps its own id %s"
                 " (request #%" PRIu64 ")", request.peer.c_str(), id.c_str(),
                 backend_emu_store::player_id(), g_auth_lan);
        }
    } else {
        // No id presented: a fresh install. Offer the stored one, which the
        // game is free to take.
        id = backend_emu_store::player_id();
        ++g_auth_without_id;
        if (should_log(g_auth_without_id)) {
            LOGI("23.1.3-backend-auth: the game presented no account id"
                 " (target '%s'); it was offered the stored id %s, source"
                 " '%s' (request #%" PRIu64 ")", request.target.c_str(),
                 id.c_str(), backend_emu_store::player_id_source().c_str(),
                 g_auth_without_id);
        }
    }

    const std::string session = backend_emu_store::session_token();
    const std::string nick = backend_emu_store::nickname();

    const int64_t logins = backend_emu_store::add_int("logins", 1, 0);
    // A game that presents an id is not a new player, whatever the login
    // counter says.
    const bool first_login = logins <= 1 && presented.empty();

    std::string body("{");
    body += envelope();
    body += "," + text_member("id_player", id);
    body += "," + text_member("player_id", id);
    body += "," + text_member("id", id);
    body += "," + text_member("user_id", id);
    body += "," + text_member("account_id", id);
    body += "," + text_member("session", session);
    body += "," + text_member("session_key", session);
    body += "," + text_member("token", session);
    body += "," + text_member("auth_token", session);
    body += "," + text_member("nick", nick);
    body += "," + text_member("name", nick);
    body += "," + text_member("login", nick);
    body += "," + number_member("is_new", first_login ? 1 : 0);
    body += "," + number_member("new_player", first_login ? 1 : 0);
    body += "," + number_member("banned", 0);
    body += "," + number_member("ban", 0);
    body += "," + number_member("ban_time", 0);
    body += "," + number_member("need_update", 0);
    body += "," + number_member("force_update", 0);
    body += "}";
    answer(response, body);

    if (first_login) {
        LOGI("23.1.3-backend-emu: authenticated locally as id_player=%s"
             " (nick '%s'); the retired account service is never contacted",
             id.c_str(), nick.c_str());
    }
    return true;
}

// ------------------------------------------------------------------- config

// Remote configuration and A/B tests. An empty object means "no overrides",
// which is what makes the client fall back to the content shipped in the APK
// instead of the trimmed-down set the dead service would have dictated.
inline bool handle_config(const Request& request, Response& response) {
    (void)request;
    answer(response, "{}");
    return true;
}

// --------------------------------------------------------------- telemetry

// Event, statistics and crash sinks. The game only checks that the write was
// accepted, so accepting it locally removes the round-trip and the stall that
// came with it.
inline bool handle_sink(const Request& request, Response& response) {
    (void)request;
    answer(response, "{" + envelope() + "}");
    return true;
}

inline bool handle_geo(const Request& request, Response& response) {
    (void)request;
    std::string body("{");
    body += envelope();
    body += "," + text_member("country", "US");
    body += "," + text_member("country_code", "US");
    body += "," + text_member("continent", "NA");
    body += "," + text_member("city", "");
    body += "," + text_member("region", "");
    body += "," + text_member("ip", "127.0.0.1");
    body += "}";
    answer(response, body);
    return true;
}

// ----------------------------------------------------------- leaderboards

// Empty but well-formed boards: the screens open and stay empty instead of
// hanging on a request that can never complete.
inline bool handle_leaderboard(const Request& request, Response& response) {
    (void)request;
    std::string body("{");
    body += envelope();
    body += ",\"items\":[],\"players\":[],\"records\":[],\"list\":[]";
    body += "," + number_member("count", 0);
    body += "," + number_member("total", 0);
    body += "," + number_member("place", 0);
    body += "," + number_member("rank", 0);
    body += "}";
    answer(response, body);
    return true;
}

// ----------------------------------------------------------------- mailbox

inline bool handle_mailbox(const Request& request, Response& response) {
    (void)request;
    std::string body("{");
    body += envelope();
    body += ",\"messages\":[],\"items\":[]";
    body += "," + number_member("count", 0);
    body += "," + number_member("unread", 0);
    body += "}";
    answer(response, body);
    return true;
}

// -------------------------------------------------------------------- clans

// Clan state lives in the local store, so a clan created on the host device is
// still there after a relaunch and is visible to every device that joins this
// host over the LAN.
inline bool handle_clan(const Request& request, Response& response) {
    const std::string clan_id = backend_emu_store::get("id_clan", "");
    const std::string clan_name = backend_emu_store::get("clan_name", "");

    std::string body("{");
    body += envelope();
    body += "," + text_member("id_clan", clan_id);
    body += "," + text_member("clan_id", clan_id);
    body += "," + text_member("clan_name", clan_name);
    body += "," + text_member("name", clan_name);
    body += "," + number_member("in_clan", clan_id.empty() ? 0 : 1);
    body += "," + number_member("members_count",
                                backend_emu_store::get_int("clan_members", 0));
    body += ",\"members\":[],\"clans\":[],\"items\":[],\"requests\":[]";
    body += "}";
    answer(response, body);
    (void)request;
    return true;
}

// -------------------------------------------------------- matchmaking hints

// Room brokering itself is Photon's job in this port; these endpoints only
// have to stop blocking.
inline bool handle_matchmaking(const Request& request, Response& response) {
    (void)request;
    std::string body("{");
    body += envelope();
    body += ",\"rooms\":[],\"servers\":[]";
    body += "," + text_member("region", "");
    body += "," + text_member("mm_url", "");
    body += "," + number_member("mm_state", 1);
    body += "}";
    answer(response, body);
    return true;
}

// --------------------------------------------------------------- diagnostics

// Not a game endpoint: a status page for the port itself, reachable from the
// device or from any LAN client at http://<host>:<port>/opg3d/status. The id
// fields are the fastest way to confirm that the account the game shows and
// the account on disk are the same one.
inline bool handle_status(const Request& request, Response& response) {
    (void)request;
    std::string body("{");
    body += envelope();
    body += "," + text_member("endpoint", backend_emu_http::endpoint());
    body += "," + text_member("role",
                              backend_emu_http::is_host() ? "host" : "lan-client");
    body += "," + number_member("port", backend_emu_http::port());
    body += "," + number_member(
                      "served", static_cast<int64_t>(backend_emu_http::served()));
    body += "," + text_member("id_player", backend_emu_store::player_id());
    body += "," + text_member("id_player_source",
                              backend_emu_store::player_id_source());
    body += "," + text_member("id_player_presented",
                              backend_emu_store::get("id_player_presented", ""));
    body += "," + text_member("id_player_previous",
                              backend_emu_store::get("id_player_previous", ""));
    body += "," + text_member("nick", backend_emu_store::nickname());
    body += "," + text_member("state_dir", backend_emu_store::directory());
    body += "}";
    answer(response, body);
    return true;
}

// Everything with no route of its own. Permissive on purpose: see the header.
inline bool handle_unknown(const Request& request, Response& response) {
    (void)request;
    std::string body("{");
    body += envelope();
    body += ",\"items\":[],\"list\":[],\"data\":{}";
    body += "}";
    answer(response, body);
    return true;
}

}  // namespace detail

// Registers every endpoint. Order matters: the first matching route answers,
// so the specific paths are registered before the broad ones.
inline void install() {
    using namespace detail;

    backend_emu_http::route("/opg3d/status", &handle_status);

    // Account and clock.
    backend_emu_http::route("/auth_v2", &handle_auth);
    backend_emu_http::route("auth", &handle_auth);
    backend_emu_http::route("get_time.php", &handle_time);
    backend_emu_http::route("/time", &handle_time);

    // Remote configuration and experiments.
    backend_emu_http::route("pg3d-config-system", &handle_config);
    backend_emu_http::route("pixelgun3d-config", &handle_config);
    backend_emu_http::route("ABTests", &handle_config);
    backend_emu_http::route("config.json", &handle_config);
    backend_emu_http::route("event_x3_", &handle_config);

    // Telemetry, crash and geo sinks.
    backend_emu_http::route("add_event.php", &handle_sink);
    backend_emu_http::route("log_stat_event.php", &handle_sink);
    backend_emu_http::route("event_stat_add_pl.php", &handle_sink);
    backend_emu_http::route("/problem", &handle_sink);
    backend_emu_http::route("mapstats", &handle_sink);
    backend_emu_http::route("get_geo_info.php", &handle_geo);
    backend_emu_http::route("player-prefs", &handle_sink);

    // Social surfaces.
    backend_emu_http::route("leaderboard", &handle_leaderboard);
    backend_emu_http::route("mailbox", &handle_mailbox);
    backend_emu_http::route("clan", &handle_clan);
    backend_emu_http::route("invite_service", &handle_sink);
    backend_emu_http::route("subscribe", &handle_sink);

    // Matchmaking hints.
    backend_emu_http::route("mm_url", &handle_matchmaking);
    backend_emu_http::route("mm_state", &handle_matchmaking);
    backend_emu_http::route("mmc_get_room", &handle_matchmaking);
    backend_emu_http::route("matchmaking", &handle_matchmaking);

    backend_emu_http::set_fallback(&handle_unknown);
}

}  // namespace backend_emu_routes
