#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) local backend - in-process HTTP server and LAN discovery
//
// The retired official services are emulated inside this library. There is no
// component to deploy and nothing for a player to configure: the backend is a
// socket, a router and a set of handlers that come up with the APK itself.
//
// Two roles, decided automatically at startup
// -------------------------------------------
//   * host   - nobody answered the LAN probe, so this instance binds the HTTP
//              port on every interface (0.0.0.0) and answers discovery probes;
//   * client - another instance on the same network answered first, so this
//              instance points the game at that peer instead of binding its
//              own port.
//
// That is what makes LAN play work: the first device to launch the game owns
// the emulated backend, every device that starts later joins it, and there is
// no address to type in anywhere.
//
// Everything is plain HTTP on a private address, never TLS: the rewrite in
// backend_emu_2313.h maps every https:// endpoint of the dead services onto
// http://<endpoint>/@<original-host>/<original-path>, so no certificate
// handling is needed and the original host stays visible to the handlers.
// -----------------------------------------------------------------------------

#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "log.h"

namespace backend_emu_http {

// A parsed request. `host` is the backend host the game originally addressed,
// recovered from the /@host/ prefix the rewrite adds, so a single server can
// answer for the auth, config, stats and leaderboard hosts at once.
struct Request {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string body;
    std::string content_type;
    std::string host;
    std::string peer;

    bool has(const char* name) const;
    std::string param(const char* name, const char* fallback = "") const;
};

struct Response {
    int status = 200;
    std::string content_type = "application/json; charset=utf-8";
    std::string body;
};

// Returns true when the request was answered. Returning false lets the next
// matching route (and finally the fallback) try.
using Handler = bool (*)(const Request&, Response&);

namespace detail {

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// Both ports are fixed so that a joining device finds the host without any
// configuration, and both sit far outside the ephemeral range.
constexpr uint16_t kHttpPortPreferred = 47317u;
constexpr uint16_t kDiscoveryPort = 47318u;
constexpr int kPortAttempts = 8;

constexpr const char* kProbeMagic = "OPG3D-DISCOVER/1";
constexpr const char* kReplyMagic = "OPG3D-HOST/1";
constexpr int kProbeAttempts = 3;
constexpr long kProbeTimeoutUs = 300000L;

constexpr size_t kMaxHeaderBytes = 32u * 1024u;
constexpr size_t kMaxBodyBytes = 1024u * 1024u;
constexpr int kIoTimeoutSec = 5;
constexpr int kMaxConnections = 24;

constexpr uint64_t kLogBurst = 24u;
constexpr uint64_t kLogPeriod = 64u;

struct Route {
    const char* needle;
    Handler handler;
};

inline std::vector<Route> g_routes;
inline Handler g_fallback = nullptr;
inline std::string g_endpoint;
inline uint16_t g_port = 0u;
inline int g_listen_fd = -1;
inline uint64_t g_instance = 0u;
inline bool g_started = false;
inline bool g_host = false;
inline std::atomic<int> g_connections{0};
inline std::atomic<uint64_t> g_served{0u};
inline std::atomic<uint64_t> g_unmatched{0u};

inline bool should_log(uint64_t counter) {
    return counter <= kLogBurst || (counter % kLogPeriod) == 0u;
}

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
    value = static_cast<uint64_t>(now.tv_nsec) ^
            (static_cast<uint64_t>(now.tv_sec) << 20) ^
            (static_cast<uint64_t>(getpid()) << 40);
    return value != 0u ? value : 0x9E3779B97F4A7C15ull;
}

// ------------------------------------------------------------------ decoding

inline int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline std::string percent_decode(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0u; i < raw.size(); ++i) {
        if (raw[i] == '+') {
            out += ' ';
        } else if (raw[i] == '%' && i + 2u < raw.size()) {
            const int high = hex_value(raw[i + 1u]);
            const int low = hex_value(raw[i + 2u]);
            if (high < 0 || low < 0) {
                out += raw[i];
            } else {
                out += static_cast<char>(high * 16 + low);
                i += 2u;
            }
        } else {
            out += raw[i];
        }
    }
    return out;
}

// key=value&key=value, used by both query strings and WWWForm bodies.
inline bool find_urlencoded(const std::string& blob, const char* name,
                            std::string* out) {
    if (blob.empty() || name == nullptr) return false;
    const std::string key(name);
    size_t position = 0u;
    while (position <= blob.size()) {
        size_t end = blob.find('&', position);
        if (end == std::string::npos) end = blob.size();
        const std::string pair = blob.substr(position, end - position);
        position = end + 1u;
        if (pair.empty()) continue;
        const size_t equals = pair.find('=');
        const std::string candidate =
            equals == std::string::npos ? pair : pair.substr(0u, equals);
        if (percent_decode(candidate) != key) continue;
        if (out != nullptr) {
            *out = equals == std::string::npos
                       ? std::string()
                       : percent_decode(pair.substr(equals + 1u));
        }
        return true;
    }
    return false;
}

// WWWForm switches to multipart/form-data as soon as a binary field is added,
// so both encodings have to be understood.
inline bool find_multipart(const std::string& body, const char* name,
                           std::string* out) {
    if (body.empty() || name == nullptr) return false;
    const std::string marker = std::string("name=\"") + name + "\"";
    const size_t at = body.find(marker);
    if (at == std::string::npos) return false;
    const size_t blank = body.find("\r\n\r\n", at);
    if (blank == std::string::npos) return false;
    const size_t start = blank + 4u;
    size_t end = body.find("\r\n--", start);
    if (end == std::string::npos) end = body.size();
    if (out != nullptr) *out = body.substr(start, end - start);
    return true;
}

}  // namespace detail

inline bool Request::has(const char* name) const {
    if (detail::find_urlencoded(query, name, nullptr)) return true;
    if (content_type.find("multipart/") != std::string::npos) {
        return detail::find_multipart(body, name, nullptr);
    }
    return detail::find_urlencoded(body, name, nullptr);
}

inline std::string Request::param(const char* name, const char* fallback) const {
    std::string value;
    if (detail::find_urlencoded(query, name, &value)) return value;
    if (content_type.find("multipart/") != std::string::npos) {
        if (detail::find_multipart(body, name, &value)) return value;
    } else if (detail::find_urlencoded(body, name, &value)) {
        return value;
    }
    return fallback != nullptr ? std::string(fallback) : std::string();
}

namespace detail {

// ---------------------------------------------------------------- request I/O

inline void set_timeouts(int fd) {
    timeval timeout {};
    timeout.tv_sec = kIoTimeoutSec;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

inline bool send_all(int fd, const char* data, size_t size) {
    size_t sent = 0u;
    while (sent < size) {
        const ssize_t produced = send(fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (produced <= 0) {
            if (produced < 0 && errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(produced);
    }
    return true;
}

inline std::string header_value(const std::string& head, const char* name) {
    const size_t name_length = std::strlen(name);
    size_t position = head.find("\r\n");
    while (position != std::string::npos) {
        position += 2u;
        size_t end = head.find("\r\n", position);
        if (end == std::string::npos) end = head.size();
        if (end > position) {
            const std::string line = head.substr(position, end - position);
            const size_t colon = line.find(':');
            if (colon == name_length &&
                strncasecmp(line.c_str(), name, name_length) == 0) {
                size_t start = colon + 1u;
                while (start < line.size() &&
                       (line[start] == ' ' || line[start] == '\t')) {
                    ++start;
                }
                return line.substr(start);
            }
        }
        position = head.find("\r\n", position);
    }
    return std::string();
}

inline bool read_request(int fd, Request* request) {
    std::string buffer;
    size_t separator = std::string::npos;
    char chunk[4096];
    while (separator == std::string::npos) {
        const ssize_t got = recv(fd, chunk, sizeof(chunk), 0);
        if (got <= 0) {
            if (got < 0 && errno == EINTR) continue;
            return false;
        }
        buffer.append(chunk, static_cast<size_t>(got));
        separator = buffer.find("\r\n\r\n");
        if (separator == std::string::npos && buffer.size() > kMaxHeaderBytes) {
            return false;
        }
    }

    const std::string head = buffer.substr(0u, separator);
    request->body = buffer.substr(separator + 4u);

    size_t line_end = head.find("\r\n");
    if (line_end == std::string::npos) line_end = head.size();
    const std::string line = head.substr(0u, line_end);
    const size_t method_end = line.find(' ');
    if (method_end == std::string::npos) return false;
    const size_t target_end = line.find(' ', method_end + 1u);
    request->method = line.substr(0u, method_end);
    request->target = line.substr(
        method_end + 1u,
        target_end == std::string::npos ? std::string::npos
                                       : target_end - method_end - 1u);

    request->content_type = header_value(head, "Content-Type");
    const std::string length_text = header_value(head, "Content-Length");
    if (!length_text.empty()) {
        const long long declared = std::strtoll(length_text.c_str(), nullptr, 10);
        if (declared > 0 && static_cast<size_t>(declared) <= kMaxBodyBytes) {
            const size_t wanted = static_cast<size_t>(declared);
            while (request->body.size() < wanted) {
                const ssize_t got = recv(fd, chunk, sizeof(chunk), 0);
                if (got <= 0) {
                    if (got < 0 && errno == EINTR) continue;
                    break;
                }
                request->body.append(chunk, static_cast<size_t>(got));
            }
        }
    }

    // Split the target, then peel off the /@<original-host> prefix the URL
    // rewrite adds so handlers can tell the retired hosts apart.
    const size_t question = request->target.find('?');
    request->path = question == std::string::npos
                        ? request->target
                        : request->target.substr(0u, question);
    request->query = question == std::string::npos
                         ? std::string()
                         : request->target.substr(question + 1u);
    if (request->path.size() > 2u && request->path[0] == '/' &&
        request->path[1] == '@') {
        const size_t slash = request->path.find('/', 2u);
        if (slash == std::string::npos) {
            request->host = request->path.substr(2u);
            request->path = "/";
        } else {
            request->host = request->path.substr(2u, slash - 2u);
            request->path = request->path.substr(slash);
        }
    }
    return true;
}

inline const char* status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

inline void write_response(int fd, const Response& response) {
    char head[512];
    const int written = std::snprintf(
        head, sizeof(head),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n",
        response.status, status_text(response.status),
        response.content_type.c_str(), response.body.size());
    if (written <= 0) return;
    if (!send_all(fd, head, static_cast<size_t>(written))) return;
    if (!response.body.empty()) {
        send_all(fd, response.body.data(), response.body.size());
    }
}

inline void dispatch(const Request& request, Response* response) {
    for (const Route& route : g_routes) {
        if (route.needle == nullptr || route.handler == nullptr) continue;
        if (request.path.find(route.needle) == std::string::npos &&
            request.target.find(route.needle) == std::string::npos) {
            continue;
        }
        if (route.handler(request, *response)) return;
    }
    if (g_fallback != nullptr && g_fallback(request, *response)) {
        const uint64_t unmatched = g_unmatched.fetch_add(1u) + 1u;
        if (should_log(unmatched)) {
            LOGW("23.1.3-backend-http: no route for %s %s%s (host='%s');"
                 " answered with the generic payload (miss #%" PRIu64 ")",
                 request.method.c_str(), request.host.c_str(),
                 request.path.c_str(), request.host.c_str(), unmatched);
        }
        return;
    }
    response->status = 404;
    response->body = "{\"result\":\"error\",\"error\":\"no route\"}";
}

struct Connection {
    int fd = -1;
    std::string peer;
};

inline void* connection_thread(void* argument) {
    Connection* connection = static_cast<Connection*>(argument);
    if (connection == nullptr) return nullptr;

    Request request;
    request.peer = connection->peer;
    Response response;
    if (read_request(connection->fd, &request)) {
        dispatch(request, &response);
        write_response(connection->fd, response);
        const uint64_t served = g_served.fetch_add(1u) + 1u;
        if (should_log(served)) {
            LOGI("23.1.3-backend-http: %s %s%s from %s -> %d, %zu byte(s)"
                 " [request #%" PRIu64 "%s]",
                 request.method.c_str(), request.host.c_str(),
                 request.path.c_str(), request.peer.c_str(), response.status,
                 response.body.size(), served,
                 request.body.empty() ? "" : ", with body");
        }
    }
    close(connection->fd);
    delete connection;
    g_connections.fetch_sub(1);
    return nullptr;
}

inline void* accept_thread(void*) {
    LOGI("23.1.3-backend-http: accept loop is live on 0.0.0.0:%u", g_port);
    for (;;) {
        sockaddr_in from {};
        socklen_t from_length = sizeof(from);
        const int fd = accept(g_listen_fd, reinterpret_cast<sockaddr*>(&from),
                             &from_length);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (errno == EBADF || errno == EINVAL) break;
            usleep(20000);
            continue;
        }
        if (g_connections.load() >= kMaxConnections) {
            close(fd);
            continue;
        }
        set_timeouts(fd);

        char address[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &from.sin_addr, address, sizeof(address));

        Connection* connection = new Connection();
        connection->fd = fd;
        connection->peer = address;
        g_connections.fetch_add(1);

        pthread_t thread;
        if (pthread_create(&thread, nullptr, connection_thread, connection) != 0) {
            g_connections.fetch_sub(1);
            close(fd);
            delete connection;
            continue;
        }
        pthread_detach(thread);
    }
    LOGW("23.1.3-backend-http: accept loop stopped");
    return nullptr;
}

// ----------------------------------------------------------- LAN discovery

// Sends the probe to the limited broadcast address and to every interface
// broadcast address, because Android Wi-Fi setups differ in which of the two
// actually reaches the other devices.
inline void broadcast_probe(int fd, const char* payload) {
    const size_t length = std::strlen(payload);

    sockaddr_in target {};
    target.sin_family = AF_INET;
    target.sin_port = htons(kDiscoveryPort);
    target.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(fd, payload, length, 0, reinterpret_cast<sockaddr*>(&target),
           sizeof(target));

    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0 || interfaces == nullptr) return;
    for (ifaddrs* item = interfaces; item != nullptr; item = item->ifa_next) {
        if (item->ifa_broadaddr == nullptr) continue;
        if (item->ifa_broadaddr->sa_family != AF_INET) continue;
        if ((item->ifa_flags & IFF_BROADCAST) == 0) continue;
        if ((item->ifa_flags & IFF_UP) == 0) continue;
        sockaddr_in peer = *reinterpret_cast<sockaddr_in*>(item->ifa_broadaddr);
        peer.sin_family = AF_INET;
        peer.sin_port = htons(kDiscoveryPort);
        sendto(fd, payload, length, 0, reinterpret_cast<sockaddr*>(&peer),
               sizeof(peer));
    }
    freeifaddrs(interfaces);
}

inline bool probe_for_host(std::string* endpoint) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    int enable = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));
    timeval timeout {};
    timeout.tv_sec = 0;
    timeout.tv_usec = kProbeTimeoutUs;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char probe[96];
    std::snprintf(probe, sizeof(probe), "%s %016" PRIx64, kProbeMagic, g_instance);

    const size_t reply_prefix = std::strlen(kReplyMagic);
    for (int attempt = 0; attempt < kProbeAttempts; ++attempt) {
        broadcast_probe(fd, probe);
        for (;;) {
            char buffer[256];
            sockaddr_in from {};
            socklen_t from_length = sizeof(from);
            const ssize_t got =
                recvfrom(fd, buffer, sizeof(buffer) - 1u, 0,
                         reinterpret_cast<sockaddr*>(&from), &from_length);
            if (got <= 0) break;
            buffer[got] = '\0';
            if (std::strncmp(buffer, kReplyMagic, reply_prefix) != 0) continue;
            uint64_t instance = 0u;
            unsigned int port = 0u;
            if (std::sscanf(buffer + reply_prefix, " %016" SCNx64 " %u",
                            &instance, &port) != 2) {
                continue;
            }
            if (instance == g_instance || port == 0u || port > 65535u) continue;
            char address[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, address, sizeof(address));
            if (address[0] == '\0') continue;
            char text[80];
            std::snprintf(text, sizeof(text), "%s:%u", address, port);
            if (endpoint != nullptr) *endpoint = text;
            close(fd);
            return true;
        }
    }
    close(fd);
    return false;
}

inline void* discovery_thread(void*) {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOGW("23.1.3-backend-http: LAN discovery socket failed (errno=%d);"
             " other devices will not find this host automatically", errno);
        return nullptr;
    }
    int enable = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kDiscoveryPort);
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        LOGW("23.1.3-backend-http: LAN discovery port %u is taken (errno=%d);"
             " this host stays reachable by address only", kDiscoveryPort, errno);
        close(fd);
        return nullptr;
    }
    LOGI("23.1.3-backend-http: LAN discovery responder is live on udp/%u",
         kDiscoveryPort);

    const size_t probe_prefix = std::strlen(kProbeMagic);
    uint64_t answered = 0u;
    for (;;) {
        char buffer[256];
        sockaddr_in from {};
        socklen_t from_length = sizeof(from);
        const ssize_t got = recvfrom(fd, buffer, sizeof(buffer) - 1u, 0,
                                     reinterpret_cast<sockaddr*>(&from),
                                     &from_length);
        if (got <= 0) {
            if (got < 0 && errno == EINTR) continue;
            if (got < 0 && (errno == EBADF || errno == EINVAL)) break;
            continue;
        }
        buffer[got] = '\0';
        if (std::strncmp(buffer, kProbeMagic, probe_prefix) != 0) continue;
        uint64_t instance = 0u;
        if (std::sscanf(buffer + probe_prefix, " %016" SCNx64, &instance) != 1) {
            continue;
        }
        if (instance == g_instance) continue;  // our own broadcast

        char reply[96];
        const int length = std::snprintf(reply, sizeof(reply), "%s %016" PRIx64 " %u",
                                        kReplyMagic, g_instance,
                                        static_cast<unsigned int>(g_port));
        if (length <= 0) continue;
        sendto(fd, reply, static_cast<size_t>(length), 0,
               reinterpret_cast<sockaddr*>(&from), from_length);
        ++answered;
        if (should_log(answered)) {
            char text[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
            LOGI("23.1.3-backend-http: LAN client %s was pointed at this host"
                 " (announcement #%" PRIu64 ")", text, answered);
        }
    }
    close(fd);
    return nullptr;
}

// --------------------------------------------------------------- listening

inline bool bind_http() {
    for (int attempt = 0; attempt <= kPortAttempts; ++attempt) {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        int enable = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

        // The last attempt asks the kernel for any free port: a bound backend
        // on an odd port still beats no backend at all, and LAN clients learn
        // the real port from the discovery reply anyway.
        const uint16_t wanted =
            attempt < kPortAttempts
                ? static_cast<uint16_t>(kHttpPortPreferred + attempt)
                : 0u;

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(wanted);
        if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(fd, 32) != 0) {
            close(fd);
            continue;
        }

        sockaddr_in bound {};
        socklen_t bound_length = sizeof(bound);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &bound_length) != 0) {
            close(fd);
            continue;
        }
        g_listen_fd = fd;
        g_port = ntohs(bound.sin_port);
        return true;
    }
    return false;
}

}  // namespace detail

// ---------------------------------------------------------------- public API

// Registers a handler. `needle` is matched as a substring of the request path
// (and of the raw target), so "add_event.php" and "/auth_v2/" both work.
// Routes are tried in registration order, so specific ones come first.
inline void route(const char* needle, Handler handler) {
    if (needle == nullptr || handler == nullptr) return;
    detail::g_routes.push_back(detail::Route{needle, handler});
}

// Answers everything no route claimed. Also what makes unknown endpoints
// visible in logcat instead of silently timing out inside the game.
inline void set_fallback(Handler handler) { detail::g_fallback = handler; }

// Picks the role, then either joins the LAN host or starts serving.
inline bool start() {
    if (detail::g_started) return true;
    detail::g_instance = detail::random64();

    std::string peer;
    if (detail::probe_for_host(&peer)) {
        detail::g_endpoint = peer;
        detail::g_host = false;
        detail::g_started = true;
        LOGI("23.1.3-backend-emu: joined the emulated backend already running"
             " on this network at %s; this device is a LAN client",
             detail::g_endpoint.c_str());
        return true;
    }

    if (!detail::bind_http()) {
        LOGE("23.1.3-backend-emu: no local port could be bound; the emulated"
             " backend was not started");
        return false;
    }

    pthread_t server;
    if (pthread_create(&server, nullptr, detail::accept_thread, nullptr) != 0) {
        LOGE("23.1.3-backend-emu: the server thread could not be started");
        close(detail::g_listen_fd);
        detail::g_listen_fd = -1;
        return false;
    }
    pthread_detach(server);

    pthread_t discovery;
    if (pthread_create(&discovery, nullptr, detail::discovery_thread, nullptr) == 0) {
        pthread_detach(discovery);
    } else {
        LOGW("23.1.3-backend-emu: LAN discovery was not started; only this"
             " device can reach the backend");
    }

    char text[80];
    std::snprintf(text, sizeof(text), "127.0.0.1:%u",
                  static_cast<unsigned int>(detail::g_port));
    detail::g_endpoint = text;
    detail::g_host = true;
    detail::g_started = true;
    LOGI("23.1.3-backend-emu: hosting the emulated backend on 0.0.0.0:%u"
         " (%zu route(s)); devices on this network join it automatically",
         static_cast<unsigned int>(detail::g_port), detail::g_routes.size());
    return true;
}

inline bool ready() { return detail::g_started; }
inline bool is_host() { return detail::g_host; }
inline uint16_t port() { return detail::g_port; }
inline uint64_t served() { return detail::g_served.load(); }

// "host:port" the game's traffic has to be pointed at.
inline const char* endpoint() { return detail::g_endpoint.c_str(); }

}  // namespace backend_emu_http
