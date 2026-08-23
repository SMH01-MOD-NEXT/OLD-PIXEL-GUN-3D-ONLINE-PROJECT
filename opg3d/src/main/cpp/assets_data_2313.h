#pragma once

// -----------------------------------------------------------------------------
// 23.1.3 (ARM64) in-APK asset payload provisioning (the "assets/data" loader)
//
// The game downloads its optional resources (extra maps and their asset
// bundles) from a backend that may well disappear, which would leave the
// version unusable for missing resources alone. This module lets the APK carry
// those resources itself: put the archive at assets/data/data.zip and it is
// unpacked into the very directory the game reads.
//
// Where the game looks
// --------------------
// PGCompany.AssetBundles_v3.丄七丁丅丐与丁丗与 builds the resource root as
//   Path.Combine(Application.persistentDataPath, <litA>, <litB>)
// in 专丌上丕丌丑丈丞世() (RVA 0x1D32BBC, verified in the dump), and every
// consumer goes through it: the bundle enumerator, the per-entry path helper
// and the bundle loader.
//
// So instead of guessing that directory, this module hooks that getter. The
// first call performs the extraction and then returns the stock string, which
// means the two obfuscated path components never have to be resolved, the
// destination is exactly what the game is about to read, and the timing is
// right: the getter runs before the bundles are enumerated.
//
// Tolerating an unknown archive
// -----------------------------
// The archive is assembled by hand from a device, so its exact shape cannot be
// assumed. Everything here is deliberately defensive:
//   * the destination is chosen by comparing the archive's own top-level name
//     with the last two components of the resource root, so an archive holding
//     "<litA>/<litB>/..." lands on persistentDataPath while an archive holding
//     the bundle files themselves lands directly in the resource root;
//   * one wrapper directory that contains everything ("data/", "files/", ...)
//     is stripped, because zipping a folder instead of its contents is the
//     easiest mistake to make;
//   * archiver metadata is ignored: __MACOSX, .DS_Store, Thumbs.db,
//     desktop.ini, AppleDouble "._" files, directory entries, and both the
//     archive comment and any extra fields;
//   * absolute paths, "..", backslash separators and over-long names are
//     refused rather than written somewhere unexpected;
//   * every file is extracted to "<name>.opg3d-part", verified against the
//     CRC-32 recorded in the archive and only then renamed into place, so an
//     interrupted first launch cannot leave a truncated bundle behind;
//   * a file already present with exactly the recorded size is left alone, so
//     a later launch costs a handful of stat() calls;
//   * a stamp file records the payload's CRC-32 and size, so later launches
//     skip the walk entirely while replacing data.zip re-runs it;
//   * nothing is ever deleted, free space is checked up front, and every
//     failure path logs the reason and leaves the resource root as it was.
//
// The ZIP plumbing is shared with obb_provisioner.h, extended with a base
// offset so that a stored data.zip inside the APK is parsed in place, without
// staging a second copy on disk.
// -----------------------------------------------------------------------------

#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <zlib.h>

#include "hook.h"
#include "il2cpp.h"
#include "log.h"
#include "obb_provisioner.h"

namespace assets_data_2313 {
namespace detail {

namespace obb = obb_provisioner::detail;

static_assert(sizeof(void*) == 8, "PG3D 23.1.3 target must be arm64-v8a");

// ----------------------------------------------------------- metadata names

constexpr const char* kBundlesNs = "PGCompany.AssetBundles_v3";
constexpr const char* kPathsClass = "丄七丁丅丐与丁丗与";
constexpr const char* kPersistentRoot = "专丌上丕丌丑丈丞世";

// Proves the hooked obfuscated getter really is the verified 23.1.3 ARM64 one.
constexpr uintptr_t kPersistentRootRva = 0x1D32BBCu;

// ------------------------------------------------------------------ tunables

constexpr const char* kPayloadDir = "assets/data/";
constexpr const char* kPreferredPayload = "assets/data/data.zip";
constexpr const char* kStampName = ".opg3d-data.stamp";
constexpr const char* kPartSuffix = ".opg3d-part";
constexpr const char* kStagedName = ".opg3d-data-staged.zip";

constexpr uint64_t kDirectoryCap = 64u * 1024u * 1024u;
constexpr uint64_t kFreeSpaceMargin = 64u * 1024u * 1024u;
constexpr uint64_t kProgressStep = 64u * 1024u * 1024u;
constexpr size_t kMaxRelative = 512u;

// ---------------------------------------------------------------- archives
//
// Every offset below is relative to `base`, so the same reader parses the APK
// (base 0) and a stored data.zip inside it (base = its data offset).

struct Archive {
    int fd = -1;
    uint64_t base = 0u;
    uint64_t size = 0u;
};

struct Directory {
    uint8_t* data = nullptr;
    uint64_t size = 0u;
    uint64_t count = 0u;
};

inline void release(Directory* directory) {
    if (directory == nullptr) return;
    std::free(directory->data);
    directory->data = nullptr;
    directory->size = 0u;
    directory->count = 0u;
}

inline bool locate_directory(const Archive& archive, uint64_t* cd_offset,
                             uint64_t* cd_size, uint64_t* entry_count) {
    if (archive.size < 22u) return false;
    const uint64_t window =
        archive.size < obb::kEocdWindow ? archive.size : obb::kEocdWindow;
    uint8_t* tail = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(window)));
    if (tail == nullptr) return false;
    if (!obb::read_exact(archive.fd, tail, static_cast<size_t>(window),
                         archive.base + archive.size - window)) {
        std::free(tail);
        return false;
    }

    int64_t eocd = -1;
    for (int64_t i = static_cast<int64_t>(window) - 22; i >= 0; --i) {
        if (obb::le32(tail + i) == obb::kSigEocd) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        std::free(tail);
        return false;
    }

    uint64_t count = obb::le16(tail + eocd + 10);
    uint64_t size = obb::le32(tail + eocd + 12);
    uint64_t offset = obb::le32(tail + eocd + 16);

    // zip64: the locator sits immediately in front of the EOCD record.
    if (offset == obb::kZip32Sentinel || size == obb::kZip32Sentinel ||
        count == 0xffffu) {
        if (eocd >= 20 && obb::le32(tail + eocd - 20) == obb::kSigZip64Locator) {
            const uint64_t record_at = obb::le64(tail + eocd - 20 + 8);
            uint8_t record[56] = {};
            if (obb::read_exact(archive.fd, record, sizeof(record),
                                archive.base + record_at) &&
                obb::le32(record) == obb::kSigZip64Eocd) {
                count = obb::le64(record + 32);
                size = obb::le64(record + 40);
                offset = obb::le64(record + 48);
            }
        }
    }
    std::free(tail);

    if (size == 0u || size > kDirectoryCap || offset > archive.size ||
        offset + size > archive.size) {
        return false;
    }
    *cd_offset = offset;
    *cd_size = size;
    *entry_count = count;
    return true;
}

inline bool read_directory(const Archive& archive, Directory* out) {
    uint64_t offset = 0u;
    uint64_t size = 0u;
    uint64_t count = 0u;
    if (!locate_directory(archive, &offset, &size, &count)) return false;

    uint8_t* data = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(size)));
    if (data == nullptr) return false;
    if (!obb::read_exact(archive.fd, data, static_cast<size_t>(size),
                         archive.base + offset)) {
        std::free(data);
        return false;
    }
    out->data = data;
    out->size = size;
    out->count = count;
    return true;
}

// Reads the zip64 extra field of a central-directory record and replaces the
// 32-bit sentinels with their real 64-bit values.
inline void apply_zip64(const uint8_t* extra, size_t extra_length,
                        obb::ZipEntry* entry) {
    size_t cursor = 0u;
    while (cursor + 4u <= extra_length) {
        const uint16_t id = obb::le16(extra + cursor);
        const uint16_t size = obb::le16(extra + cursor + 2);
        if (cursor + 4u + size > extra_length) return;
        if (id == 0x0001u) {
            size_t field = cursor + 4u;
            if (entry->uncompressed == obb::kZip32Sentinel &&
                field + 8u <= cursor + 4u + size) {
                entry->uncompressed = obb::le64(extra + field);
                field += 8u;
            }
            if (entry->compressed == obb::kZip32Sentinel &&
                field + 8u <= cursor + 4u + size) {
                entry->compressed = obb::le64(extra + field);
                field += 8u;
            }
            if (entry->local_offset == obb::kZip32Sentinel &&
                field + 8u <= cursor + 4u + size) {
                entry->local_offset = obb::le64(extra + field);
            }
            return;
        }
        cursor += 4u + size;
    }
}

// Central-directory cursor. Returns false once the directory is exhausted or a
// record stops making sense.
inline bool next_entry(const Directory& directory, uint64_t* cursor,
                       obb::ZipEntry* out) {
    if (directory.data == nullptr || *cursor + 46u > directory.size) return false;
    const uint8_t* record = directory.data + *cursor;
    if (obb::le32(record) != obb::kSigCentral) return false;

    const uint16_t name_length = obb::le16(record + 28);
    const uint16_t extra_length = obb::le16(record + 30);
    const uint16_t comment_length = obb::le16(record + 32);
    const uint64_t total =
        46u + static_cast<uint64_t>(name_length) +
        static_cast<uint64_t>(extra_length) + static_cast<uint64_t>(comment_length);
    if (*cursor + total > directory.size) return false;

    obb::ZipEntry entry {};
    entry.method = obb::le16(record + 10);
    entry.crc = obb::le32(record + 16);
    entry.compressed = obb::le32(record + 20);
    entry.uncompressed = obb::le32(record + 24);
    entry.local_offset = obb::le32(record + 42);
    entry.valid = name_length > 0u && name_length < obb::kNameCap;
    if (entry.valid) {
        std::memcpy(entry.name, record + 46, name_length);
        entry.name[name_length] = '\0';
    } else {
        entry.name[0] = '\0';
    }
    if (extra_length > 0u) {
        apply_zip64(record + 46 + name_length, extra_length, &entry);
    }

    *out = entry;
    *cursor += total;
    return true;
}

inline bool data_offset_of(const Archive& archive, const obb::ZipEntry& entry,
                           uint64_t* data_offset) {
    uint8_t local[30] = {};
    if (!obb::read_exact(archive.fd, local, sizeof(local),
                         archive.base + entry.local_offset)) {
        return false;
    }
    if (obb::le32(local) != obb::kSigLocal) return false;
    *data_offset = entry.local_offset + 30u +
                   static_cast<uint64_t>(obb::le16(local + 26)) +
                   static_cast<uint64_t>(obb::le16(local + 28));
    return *data_offset + entry.compressed <= archive.size;
}

// --------------------------------------------------------------- extraction

// Streams one entry out of `archive` into `temporary`, verifying the recorded
// CRC-32. Stored and deflated entries are both supported; anything else is
// refused rather than written half-decoded.
inline bool extract_entry(const Archive& archive, const obb::ZipEntry& entry,
                          uint64_t data_offset, const char* temporary) {
    if (entry.method != obb::kMethodStore && entry.method != obb::kMethodDeflate) {
        LOGE("23.1.3-assets-data: '%s' uses unsupported compression method %u",
             entry.name, static_cast<unsigned>(entry.method));
        return false;
    }

    const int out = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) {
        LOGE("23.1.3-assets-data: cannot create '%s': %s", temporary,
             std::strerror(errno));
        return false;
    }

    uint8_t* input = static_cast<uint8_t*>(std::malloc(obb::kChunk));
    uint8_t* output = static_cast<uint8_t*>(std::malloc(obb::kChunk));
    if (input == nullptr || output == nullptr) {
        LOGE("23.1.3-assets-data: out of memory while extracting '%s'", entry.name);
        std::free(input);
        std::free(output);
        close(out);
        unlink(temporary);
        return false;
    }

    const bool inflating = entry.method == obb::kMethodDeflate;
    z_stream stream {};
    if (inflating && inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        LOGE("23.1.3-assets-data: inflateInit2 failed for '%s'", entry.name);
        std::free(input);
        std::free(output);
        close(out);
        unlink(temporary);
        return false;
    }

    bool ok = true;
    uint64_t remaining = entry.compressed;
    uint64_t written = 0u;
    uint64_t read_at = archive.base + data_offset;
    uLong crc = crc32(0uL, Z_NULL, 0);

    while (ok && (remaining > 0u || (inflating && stream.avail_in > 0u))) {
        if (remaining > 0u && stream.avail_in == 0u) {
            const size_t take = remaining < obb::kChunk
                                    ? static_cast<size_t>(remaining)
                                    : obb::kChunk;
            if (!obb::read_exact(archive.fd, input, take, read_at)) {
                LOGE("23.1.3-assets-data: read failed inside '%s': %s",
                     entry.name, std::strerror(errno));
                ok = false;
                break;
            }
            read_at += take;
            remaining -= take;

            if (!inflating) {
                crc = crc32(crc, input, static_cast<uInt>(take));
                if (!obb::write_exact(out, input, take)) {
                    LOGE("23.1.3-assets-data: write failed for '%s': %s",
                         entry.name, std::strerror(errno));
                    ok = false;
                    break;
                }
                written += take;
                continue;
            }
            stream.next_in = input;
            stream.avail_in = static_cast<uInt>(take);
        }
        if (!inflating) continue;

        stream.next_out = output;
        stream.avail_out = static_cast<uInt>(obb::kChunk);
        const int status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            LOGE("23.1.3-assets-data: inflate failed for '%s' (zlib %d)",
                 entry.name, status);
            ok = false;
            break;
        }
        const size_t produced = obb::kChunk - stream.avail_out;
        if (produced > 0u) {
            crc = crc32(crc, output, static_cast<uInt>(produced));
            if (!obb::write_exact(out, output, produced)) {
                LOGE("23.1.3-assets-data: write failed for '%s': %s", entry.name,
                     std::strerror(errno));
                ok = false;
                break;
            }
            written += produced;
        }
        if (status == Z_STREAM_END) break;
        if (produced == 0u && stream.avail_in == 0u && remaining == 0u) break;
    }

    if (inflating) inflateEnd(&stream);
    if (ok) ok = fsync(out) == 0;
    close(out);
    std::free(input);
    std::free(output);

    if (ok && written != entry.uncompressed) {
        LOGE("23.1.3-assets-data: '%s' produced %" PRIu64 " of %" PRIu64
             " bytes", entry.name, written, entry.uncompressed);
        ok = false;
    }
    if (ok && static_cast<uint32_t>(crc) != entry.crc) {
        LOGE("23.1.3-assets-data: CRC mismatch for '%s' (got %08" PRIx32
             ", expected %08" PRIx32 ")", entry.name,
             static_cast<uint32_t>(crc), entry.crc);
        ok = false;
    }
    if (!ok) unlink(temporary);
    return ok;
}

// ------------------------------------------------------------------- paths

inline bool ensure_directory_tree(const char* path) {
    if (path == nullptr || path[0] != '/') return false;
    char buffer[obb::kPathCap];
    if (std::snprintf(buffer, sizeof(buffer), "%s", path) < 0) return false;
    if (std::strlen(path) >= sizeof(buffer)) return false;

    for (char* cursor = buffer + 1; *cursor != '\0'; ++cursor) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        if (!obb::ensure_directory(buffer)) return false;
        *cursor = '/';
    }
    return obb::ensure_directory(buffer);
}

inline bool parent_of(const char* path, char* out, size_t capacity) {
    const char* slash = std::strrchr(path, '/');
    if (slash == nullptr || slash == path) return false;
    const size_t length = static_cast<size_t>(slash - path);
    if (length + 1u > capacity) return false;
    std::memcpy(out, path, length);
    out[length] = '\0';
    return true;
}

inline bool equals_ci(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return false;
    while (*left != '\0' && *right != '\0') {
        if (obb::lower(*left) != obb::lower(*right)) return false;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

// Copies the first path component of `name` ("a/b/c" -> "a").
inline void head_component(const char* name, char* out, size_t capacity) {
    out[0] = '\0';
    const char* slash = std::strchr(name, '/');
    const size_t length = slash != nullptr
                              ? static_cast<size_t>(slash - name)
                              : std::strlen(name);
    if (length == 0u || length + 1u > capacity) return;
    std::memcpy(out, name, length);
    out[length] = '\0';
}

// ------------------------------------------------------------ entry filter

inline bool is_archiver_metadata(const char* name) {
    const char* base = obb::base_name_of(name);
    return obb::starts_with_ci(name, "__MACOSX/") ||
           obb::starts_with_ci(base, "._") ||
           equals_ci(base, ".DS_Store") ||
           equals_ci(base, "Thumbs.db") ||
           equals_ci(base, "desktop.ini") ||
           equals_ci(base, ".directory") ||
           equals_ci(base, kStampName) ||
           obb::ends_with_ci(name, kPartSuffix);
}

inline bool is_safe_relative(const char* name) {
    if (name[0] == '\0' || name[0] == '/') return false;
    if (std::strchr(name, '\\') != nullptr) return false;
    if (std::strlen(name) >= kMaxRelative) return false;
    if (std::strstr(name, "../") != nullptr) return false;
    if (std::strncmp(name, "..", 2) == 0 && name[2] == '\0') return false;
    // Reject a drive-letter style prefix such as "C:".
    return std::strchr(name, ':') == nullptr;
}

inline bool is_directory_entry(const obb::ZipEntry& entry) {
    const size_t length = std::strlen(entry.name);
    return length > 0u && entry.name[length - 1u] == '/';
}

inline bool is_payload_candidate(const char* name) {
    return obb::starts_with_ci(name, kPayloadDir) &&
           obb::ends_with_ci(name, ".zip");
}

// --------------------------------------------------------------- bookkeeping

struct Stats {
    uint64_t written = 0u;
    uint64_t skipped = 0u;
    uint64_t ignored = 0u;
    uint64_t failed = 0u;
    uint64_t bytes = 0u;
};

inline std::atomic<bool> g_done{false};
inline bool g_installed = false;

using StaticStringFn = void* (*)(void* method);
inline StaticStringFn g_orig_bundle_root = nullptr;

inline bool stamp_matches(const char* root, const obb::ZipEntry& payload) {
    char path[obb::kPathCap];
    std::snprintf(path, sizeof(path), "%s/%s", root, kStampName);
    FILE* file = std::fopen(path, "re");
    if (file == nullptr) return false;
    char line[128] = {};
    const bool read = std::fgets(line, sizeof(line), file) != nullptr;
    std::fclose(file);
    if (!read) return false;

    char expected[128];
    std::snprintf(expected, sizeof(expected), "%08" PRIx32 ":%" PRIu64,
                  payload.crc, payload.uncompressed);
    return std::strncmp(line, expected, std::strlen(expected)) == 0;
}

inline void write_stamp(const char* root, const obb::ZipEntry& payload) {
    char path[obb::kPathCap];
    std::snprintf(path, sizeof(path), "%s/%s", root, kStampName);
    FILE* file = std::fopen(path, "we");
    if (file == nullptr) {
        LOGW("23.1.3-assets-data: cannot write the stamp '%s': %s", path,
             std::strerror(errno));
        return;
    }
    std::fprintf(file, "%08" PRIx32 ":%" PRIu64 "\n", payload.crc,
                 payload.uncompressed);
    std::fclose(file);
}

inline bool free_space_ok(const char* directory, uint64_t needed) {
    struct statvfs stats {};
    if (statvfs(directory, &stats) != 0) return true;  // unknown: let I/O decide
    const uint64_t free_bytes = static_cast<uint64_t>(stats.f_bavail) *
                                static_cast<uint64_t>(stats.f_frsize);
    if (free_bytes >= needed + kFreeSpaceMargin) return true;
    LOGE("23.1.3-assets-data: not enough free space on '%s': %" PRIu64
         " MiB free, %" PRIu64 " MiB needed", directory,
         free_bytes / (1024u * 1024u),
         (needed + kFreeSpaceMargin) / (1024u * 1024u));
    return false;
}

// ------------------------------------------------------------ layout choice

// Reports the archive's single shared top-level directory, if there is one, so
// an accidental wrapper folder can be stripped. Also totals the payload size.
inline void survey(const Directory& directory, char* shared_head,
                   size_t head_capacity, uint64_t* total_bytes,
                   uint64_t* usable_entries) {
    shared_head[0] = '\0';
    *total_bytes = 0u;
    *usable_entries = 0u;

    bool first = true;
    bool shared = true;
    char candidate[obb::kNameCap];
    candidate[0] = '\0';

    uint64_t cursor = 0u;
    obb::ZipEntry entry {};
    while (next_entry(directory, &cursor, &entry)) {
        if (!entry.valid || is_directory_entry(entry) ||
            is_archiver_metadata(entry.name) || !is_safe_relative(entry.name)) {
            continue;
        }
        ++(*usable_entries);
        *total_bytes += entry.uncompressed;

        char head[obb::kNameCap];
        head_component(entry.name, head, sizeof(head));
        const bool nested = std::strchr(entry.name, '/') != nullptr;
        if (!nested || head[0] == '\0') {
            shared = false;
            continue;
        }
        if (first) {
            std::snprintf(candidate, sizeof(candidate), "%s", head);
            first = false;
        } else if (!equals_ci(candidate, head)) {
            shared = false;
        }
    }

    if (shared && !first) {
        std::snprintf(shared_head, head_capacity, "%s", candidate);
    }
}

// True when `head` looks like a folder somebody zipped by accident rather than
// a resource directory the game expects.
inline bool is_wrapper_name(const char* head) {
    return equals_ci(head, "data") || equals_ci(head, "files") ||
           equals_ci(head, "data.zip") || equals_ci(head, "resources") ||
           equals_ci(head, "persistentdatapath") ||
           equals_ci(head, "persistent") || equals_ci(head, "assets");
}

// The resource root is persistentDataPath/<litA>/<litB>. Depending on where the
// archive was created its top-level name may be <litA> or <litB>, so the
// destination is lifted accordingly. Anything else extracts into the root.
inline void choose_destination(const char* root, const char* head, char* out,
                               size_t capacity, const char** reason) {
    std::snprintf(out, capacity, "%s", root);
    *reason = "archive holds the bundle files themselves";
    if (head == nullptr || head[0] == '\0') return;

    char parent[obb::kPathCap];
    if (!parent_of(root, parent, sizeof(parent))) return;

    if (equals_ci(obb::base_name_of(root), head)) {
        std::snprintf(out, capacity, "%s", parent);
        *reason = "archive already contains the resource directory";
        return;
    }

    char grandparent[obb::kPathCap];
    if (!parent_of(parent, grandparent, sizeof(grandparent))) return;
    if (equals_ci(obb::base_name_of(parent), head)) {
        std::snprintf(out, capacity, "%s", grandparent);
        *reason = "archive contains both resource directories";
    }
}

// ------------------------------------------------------------- the unpacking

inline bool unpack(const Archive& source, const Directory& directory,
                   const char* destination, const char* strip, Stats* stats) {
    uint64_t cursor = 0u;
    uint64_t next_progress = kProgressStep;
    obb::ZipEntry entry {};

    while (next_entry(directory, &cursor, &entry)) {
        if (!entry.valid || is_directory_entry(entry)) continue;
        if (is_archiver_metadata(entry.name)) {
            ++stats->ignored;
            continue;
        }
        if (!is_safe_relative(entry.name)) {
            ++stats->ignored;
            LOGW("23.1.3-assets-data: refused the unsafe entry name '%s'",
                 entry.name);
            continue;
        }

        const char* relative = entry.name;
        if (strip != nullptr && strip[0] != '\0') {
            const size_t length = std::strlen(strip);
            if (obb::starts_with_ci(relative, strip) &&
                relative[length] == '/') {
                relative += length + 1u;
            }
            if (relative[0] == '\0') continue;
        }

        char final_path[obb::kPathCap];
        if (std::snprintf(final_path, sizeof(final_path), "%s/%s", destination,
                          relative) < 0 ||
            std::strlen(destination) + std::strlen(relative) + 2u >
                sizeof(final_path)) {
            ++stats->failed;
            LOGW("23.1.3-assets-data: '%s' does not fit into a path buffer",
                 entry.name);
            continue;
        }

        struct stat64 existing {};
        if (stat64(final_path, &existing) == 0 && S_ISREG(existing.st_mode) &&
            static_cast<uint64_t>(existing.st_size) == entry.uncompressed) {
            ++stats->skipped;
            continue;
        }

        char parent[obb::kPathCap];
        if (parent_of(final_path, parent, sizeof(parent)) &&
            !ensure_directory_tree(parent)) {
            ++stats->failed;
            LOGW("23.1.3-assets-data: cannot create '%s': %s", parent,
                 std::strerror(errno));
            continue;
        }

        uint64_t data_offset = 0u;
        if (!data_offset_of(source, entry, &data_offset)) {
            ++stats->failed;
            LOGW("23.1.3-assets-data: '%s' has an unreadable local header",
                 entry.name);
            continue;
        }

        char temporary[obb::kPathCap];
        std::snprintf(temporary, sizeof(temporary), "%s%s", final_path,
                      kPartSuffix);
        if (!extract_entry(source, entry, data_offset, temporary)) {
            ++stats->failed;
            continue;
        }
        if (rename(temporary, final_path) != 0) {
            ++stats->failed;
            LOGW("23.1.3-assets-data: cannot move '%s' into place: %s",
                 final_path, std::strerror(errno));
            unlink(temporary);
            continue;
        }

        ++stats->written;
        stats->bytes += entry.uncompressed;
        if (stats->bytes >= next_progress) {
            LOGI("23.1.3-assets-data: unpacked %" PRIu64 " MiB in %" PRIu64
                 " files", stats->bytes / (1024u * 1024u), stats->written);
            next_progress = stats->bytes + kProgressStep;
        }
    }
    return stats->failed == 0u;
}

// Runs the whole provisioning for the given resource root. Fail-soft: any
// problem is logged and the resource root is left exactly as it was.
inline void run(const char* root) {
    if (root == nullptr || root[0] != '/') {
        LOGW("23.1.3-assets-data: the game reported an unusable resource root;"
             " nothing to provision");
        return;
    }

    char apk[obb::kPathCap];
    if (!obb::find_own_apk(apk, sizeof(apk))) {
        LOGW("23.1.3-assets-data: own APK not found in /proc/self/maps;"
             " nothing to provision");
        return;
    }

    const int apk_fd = open(apk, O_RDONLY | O_CLOEXEC);
    if (apk_fd < 0) {
        LOGW("23.1.3-assets-data: cannot open '%s': %s", apk,
             std::strerror(errno));
        return;
    }
    struct stat64 apk_info {};
    if (stat64(apk, &apk_info) != 0) {
        LOGW("23.1.3-assets-data: cannot stat '%s': %s", apk,
             std::strerror(errno));
        close(apk_fd);
        return;
    }

    Archive container;
    container.fd = apk_fd;
    container.base = 0u;
    container.size = static_cast<uint64_t>(apk_info.st_size);

    Directory outer {};
    if (!read_directory(container, &outer)) {
        LOGW("23.1.3-assets-data: '%s' has no readable ZIP directory", apk);
        close(apk_fd);
        return;
    }

    // Pick the payload: assets/data/data.zip wins, otherwise the largest .zip
    // in assets/data/.
    obb::ZipEntry payload {};
    bool exact = false;
    uint64_t cursor = 0u;
    obb::ZipEntry entry {};
    while (next_entry(outer, &cursor, &entry)) {
        if (!entry.valid || !is_payload_candidate(entry.name)) continue;
        if (equals_ci(entry.name, kPreferredPayload)) {
            payload = entry;
            exact = true;
            break;
        }
        if (!exact && entry.uncompressed > payload.uncompressed) payload = entry;
    }
    release(&outer);

    if (!payload.valid) {
        LOGI("23.1.3-assets-data: this APK carries no '%s' payload; nothing to"
             " provision (the game keeps using its own downloader)",
             kPreferredPayload);
        close(apk_fd);
        return;
    }
    LOGI("23.1.3-assets-data: payload '%s' (%" PRIu64 " MiB, crc %08" PRIx32
         "), resource root '%s'", payload.name,
         payload.uncompressed / (1024u * 1024u), payload.crc, root);

    if (stamp_matches(root, payload)) {
        LOGI("23.1.3-assets-data: this payload is already provisioned; skipping");
        close(apk_fd);
        return;
    }

    // A stored payload is parsed in place; a deflated one is staged next to the
    // resource root first, because ZIP needs random access.
    uint64_t payload_offset = 0u;
    if (!data_offset_of(container, payload, &payload_offset)) {
        LOGW("23.1.3-assets-data: '%s' has an unreadable local header",
             payload.name);
        close(apk_fd);
        return;
    }

    if (!ensure_directory_tree(root)) {
        LOGW("23.1.3-assets-data: cannot create the resource root '%s': %s",
             root, std::strerror(errno));
        close(apk_fd);
        return;
    }

    Archive inner;
    char staged[obb::kPathCap];
    staged[0] = '\0';
    int staged_fd = -1;

    if (payload.method == obb::kMethodStore) {
        inner.fd = apk_fd;
        inner.base = payload_offset;
        inner.size = payload.uncompressed;
    } else {
        std::snprintf(staged, sizeof(staged), "%s/%s", root, kStagedName);
        if (!free_space_ok(root, payload.uncompressed * 2u)) {
            close(apk_fd);
            return;
        }
        LOGI("23.1.3-assets-data: the payload is deflated; staging it at '%s'",
             staged);
        if (!extract_entry(container, payload, payload_offset, staged)) {
            close(apk_fd);
            return;
        }
        staged_fd = open(staged, O_RDONLY | O_CLOEXEC);
        if (staged_fd < 0) {
            LOGW("23.1.3-assets-data: cannot reopen '%s': %s", staged,
                 std::strerror(errno));
            unlink(staged);
            close(apk_fd);
            return;
        }
        inner.fd = staged_fd;
        inner.base = 0u;
        inner.size = payload.uncompressed;
    }

    Directory contents {};
    if (!read_directory(inner, &contents)) {
        LOGW("23.1.3-assets-data: the payload is not a readable ZIP archive;"
             " nothing was written");
        if (staged_fd >= 0) close(staged_fd);
        if (staged[0] != '\0') unlink(staged);
        close(apk_fd);
        return;
    }

    char shared_head[obb::kNameCap];
    uint64_t total_bytes = 0u;
    uint64_t usable = 0u;
    survey(contents, shared_head, sizeof(shared_head), &total_bytes, &usable);

    if (usable == 0u) {
        LOGW("23.1.3-assets-data: the payload has no usable files (only"
             " archiver metadata); nothing was written");
        release(&contents);
        if (staged_fd >= 0) close(staged_fd);
        if (staged[0] != '\0') unlink(staged);
        close(apk_fd);
        return;
    }

    char destination[obb::kPathCap];
    const char* reason = "";
    choose_destination(root, shared_head, destination, sizeof(destination),
                       &reason);

    const char* strip = nullptr;
    if (shared_head[0] != '\0' && is_wrapper_name(shared_head) &&
        equals_ci(destination, root)) {
        strip = shared_head;
        LOGI("23.1.3-assets-data: stripping the wrapper directory '%s/'",
             shared_head);
    }

    LOGI("23.1.3-assets-data: %" PRIu64 " files (%" PRIu64 " MiB) -> '%s' (%s)",
         usable, total_bytes / (1024u * 1024u), destination, reason);

    Stats stats {};
    if (free_space_ok(destination, total_bytes) &&
        ensure_directory_tree(destination)) {
        unpack(inner, contents, destination, strip, &stats);
    }

    release(&contents);
    if (staged_fd >= 0) close(staged_fd);
    if (staged[0] != '\0') unlink(staged);
    close(apk_fd);

    if (stats.failed == 0u) {
        write_stamp(root, payload);
        LOGI("23.1.3-assets-data: provisioning complete (written=%" PRIu64
             " already present=%" PRIu64 " ignored=%" PRIu64 ", %" PRIu64
             " MiB unpacked)", stats.written, stats.skipped, stats.ignored,
             stats.bytes / (1024u * 1024u));
    } else {
        LOGE("23.1.3-assets-data: provisioning incomplete (written=%" PRIu64
             " already present=%" PRIu64 " ignored=%" PRIu64 " failed=%" PRIu64
             "); no stamp was written, so the next launch retries",
             stats.written, stats.skipped, stats.ignored, stats.failed);
    }
}

// --------------------------------------------------------------------- hook

inline void* bundle_root_hook(void* method) {
    void* root = g_orig_bundle_root != nullptr ? g_orig_bundle_root(method)
                                               : nullptr;
    bool expected = false;
    if (g_done.compare_exchange_strong(expected, true)) {
        const std::string utf8 = il2cpp::to_utf8(root, obb::kPathCap - 1u);
        run(utf8.c_str());
    }
    return root;
}

// ------------------------------------------------------------- installation

inline bool install(uintptr_t il2cpp_base) {
    if (g_installed) return true;

    void* info = il2cpp::find_method_info(kBundlesNs, kPathsClass,
                                          kPersistentRoot, 0);
    if (info == nullptr) {
        LOGE("23.1.3-assets-data: the resource root getter is missing from"
             " metadata; the payload loader was not armed");
        return false;
    }
    void* pointer = il2cpp::method_pointer(info);
    if (il2cpp_base == 0u || pointer == nullptr) {
        LOGE("23.1.3-assets-data: the resource root getter has no usable"
             " address; the payload loader was not armed");
        return false;
    }

    const auto expected =
        reinterpret_cast<void*>(il2cpp_base + kPersistentRootRva);
    if (pointer != expected) {
        LOGE("23.1.3-assets-data: the resource root getter is at %p but RVA"
             " 0x%08" PRIxPTR " maps to %p; this is not the verified 23.1.3"
             " ARM64 image", pointer, kPersistentRootRva, expected);
        return false;
    }

    if (!hook::install({kBundlesNs, kPathsClass, kPersistentRoot, 0},
                       reinterpret_cast<void*>(&bundle_root_hook),
                       reinterpret_cast<void**>(&g_orig_bundle_root), true)) {
        LOGE("23.1.3-assets-data: the resource root getter could not be"
             " hooked");
        return false;
    }

    g_installed = true;
    LOGI("23.1.3-assets-data: armed: '%s' inside the APK is unpacked into the"
         " game's own resource root on first use", kPreferredPayload);
    return true;
}

}  // namespace detail

// Arms the in-APK asset payload loader. `il2cpp_base` is the load address of
// libil2cpp.so, used to prove the obfuscated getter is the verified one.
inline bool install_hooks(uintptr_t il2cpp_base) {
    return detail::install(il2cpp_base);
}

}  // namespace assets_data_2313
