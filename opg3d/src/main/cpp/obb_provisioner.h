#pragma once

// OBB self-provisioning: the expansion file ships inside the APK and is
// unpacked to /storage/emulated/0/Android/obb/<package>/ before Unity starts.
//
// Why this runs where it runs
// ---------------------------
// Unity resolves its expansion file while libunity/libil2cpp initialise, so
// anything that happens after that point is too late: the game has already
// concluded that its data is missing. The earliest code of ours that runs in
// the game process is the ELF constructor of this library (see main.cpp),
// executed by the dynamic linker while the APK's native libraries are being
// loaded, i.e. before Unity's own native init. provision() is therefore
// called from there, synchronously.
//
// Consequences of that placement, all deliberate:
//   * no JNI. There is no guarantee that JNI_OnLoad ever runs for us (the
//     library can be pulled in as a DT_NEEDED dependency of another .so, in
//     which case only constructors run), and calling into the VM from a
//     linker constructor is not safe in general. So the package name, the
//     version code and the payload are read out of the APK with plain
//     syscalls instead of Context.getPackageName / PackageManager /
//     AssetManager;
//   * no IL2CPP and no hooks. This module never touches the runtime, so it
//     can neither be affected by nor affect the metadata-driven modules;
//   * the copy blocks the loading thread. On a first launch the user waits
//     for one sequential read of the payload out of the APK. That is the
//     price for the game finding its data on the very first frame; every
//     later launch only stats a single file.
//
// What it does, in order:
//   1. finds its own APK through /proc/self/maps;
//   2. parses the APK's ZIP central directory (zip64 aware, because an APK
//      with an embedded expansion file is large);
//   3. inflates AndroidManifest.xml and reads `package` and `versionCode`
//      straight out of the binary XML, so the destination name is derived
//      from what this build actually is. Nothing is hardcoded; a rebuild
//      with a different version code needs no source change here;
//   4. picks the payload from the APK's assets (assets/obb/*.obb preferred);
//   5. builds <external>/Android/obb/<package>/main.<versionCode>.<package>.obb,
//      creates the two directories with mode 0755 and does nothing at all if
//      a file of exactly the expected size is already there;
//   6. extracts to "<dest>.opg3d-part", verifies the CRC-32 recorded in the
//      APK, fsyncs, chmods the result 0644 and renames it into place, so an
//      interrupted first launch can never leave a half-written expansion
//      file that the game would then try to read;
//   7. repeats the whole placement for a patch payload if the APK has one.
//
// Nothing here is destructive. An expansion file of the right size is left
// untouched, files belonging to other version codes are reported and kept,
// and every failure path leaves storage exactly as it was and says why.

#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <zlib.h>

#include "log.h"

namespace obb_provisioner {

namespace detail {

constexpr size_t kPathCap = 640;
constexpr size_t kNameCap = 320;
constexpr size_t kChunk = 1u << 20;                        // copy buffer
constexpr uint64_t kEocdWindow = 66u * 1024u;              // EOCD + comment
constexpr size_t kManifestCap = 4u * 1024u * 1024u;        // sanity cap
constexpr size_t kCentralDirectoryCap = 64u * 1024u * 1024u;
constexpr uint64_t kFreeSpaceMargin = 32u * 1024u * 1024u; // never fill the volume
constexpr uint64_t kProgressStep = 64u * 1024u * 1024u;

constexpr uint32_t kSigEocd = 0x06054b50u;
constexpr uint32_t kSigZip64Locator = 0x07064b50u;
constexpr uint32_t kSigZip64Eocd = 0x06064b50u;
constexpr uint32_t kSigCentral = 0x02014b50u;
constexpr uint32_t kSigLocal = 0x04034b50u;
constexpr uint32_t kZip32Sentinel = 0xffffffffu;

constexpr uint16_t kMethodStore = 0u;
constexpr uint16_t kMethodDeflate = 8u;

// provision() is called from two entry points (see main.cpp) and must do the
// work exactly once.
inline std::atomic<bool> g_started{false};
inline std::atomic<bool> g_result{false};

inline uint16_t le16(const uint8_t* data) {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8));
}

inline uint32_t le32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

inline uint64_t le64(const uint8_t* data) {
    return static_cast<uint64_t>(le32(data)) |
           (static_cast<uint64_t>(le32(data + 4)) << 32);
}

inline bool read_exact(int fd, void* buffer, size_t length, uint64_t offset) {
    uint8_t* out = static_cast<uint8_t*>(buffer);
    size_t done = 0u;
    while (done < length) {
        const ssize_t got = pread64(fd, out + done, length - done,
                                    static_cast<off64_t>(offset + done));
        if (got <= 0) return false;
        done += static_cast<size_t>(got);
    }
    return true;
}

inline bool write_exact(int fd, const void* buffer, size_t length) {
    const uint8_t* in = static_cast<const uint8_t*>(buffer);
    size_t done = 0u;
    while (done < length) {
        const ssize_t put = write(fd, in + done, length - done);
        if (put <= 0) {
            if (put < 0 && errno == EINTR) continue;
            return false;
        }
        done += static_cast<size_t>(put);
    }
    return true;
}

inline char lower(char value) {
    return (value >= 'A' && value <= 'Z')
               ? static_cast<char>(value - 'A' + 'a')
               : value;
}

inline bool starts_with_ci(const char* text, const char* prefix) {
    for (size_t i = 0u; prefix[i] != '\0'; ++i) {
        if (text[i] == '\0' || lower(text[i]) != lower(prefix[i])) return false;
    }
    return true;
}

inline bool ends_with_ci(const char* text, const char* suffix) {
    const size_t text_length = std::strlen(text);
    const size_t suffix_length = std::strlen(suffix);
    if (suffix_length > text_length) return false;
    const char* tail = text + (text_length - suffix_length);
    for (size_t i = 0u; i < suffix_length; ++i) {
        if (lower(tail[i]) != lower(suffix[i])) return false;
    }
    return true;
}

inline const char* base_name_of(const char* path) {
    const char* slash = std::strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

// --- our own APK -----------------------------------------------------------
//
// The mapping name for a library loaded straight out of the APK looks like
// "/data/app/~~x==/com.pixel.gun3d-y==/base.apk!/lib/armeabi-v7a/libX.so",
// so the path is cut right after ".apk". base.apk wins over any split.
inline bool find_own_apk(char* out, size_t capacity) {
    FILE* maps = std::fopen("/proc/self/maps", "re");
    if (maps == nullptr) return false;

    char best[kPathCap];
    best[0] = '\0';
    char line[1024];
    while (std::fgets(line, sizeof(line), maps) != nullptr) {
        char* path = std::strchr(line, '/');
        if (path == nullptr) continue;
        char* marker = std::strstr(path, ".apk");
        if (marker == nullptr) continue;
        marker[4] = '\0';

        struct stat64 info {};
        if (stat64(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;

        const bool is_base = std::strstr(path, "/base.apk") != nullptr;
        if (best[0] == '\0' || is_base) {
            std::snprintf(best, sizeof(best), "%s", path);
        }
        if (is_base) break;
    }
    std::fclose(maps);

    if (best[0] == '\0') return false;
    std::snprintf(out, capacity, "%s", best);
    return true;
}

// Process name, used only to cross-check the manifest and as a fallback.
inline bool read_process_name(char* out, size_t capacity) {
    const int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buffer[256];
    const ssize_t got = read(fd, buffer, sizeof(buffer) - 1u);
    close(fd);
    if (got <= 0) return false;
    buffer[got] = '\0';

    char* colon = std::strchr(buffer, ':'); // "pkg:sub-process"
    if (colon != nullptr) *colon = '\0';
    if (std::strchr(buffer, '.') == nullptr) return false;
    std::snprintf(out, capacity, "%s", buffer);
    return true;
}

// --- ZIP -------------------------------------------------------------------

struct ZipEntry {
    char name[kNameCap];
    uint16_t method;
    uint32_t crc;
    uint64_t compressed;
    uint64_t uncompressed;
    uint64_t local_offset;
    bool valid;
};

struct Catalogue {
    ZipEntry manifest;
    ZipEntry main_payload;
    ZipEntry patch_payload;
    int main_score;
    int patch_score;
    uint64_t stale_reported;
};

inline bool locate_central_directory(int fd, uint64_t file_size,
                                     uint64_t* cd_offset, uint64_t* cd_size,
                                     uint64_t* entry_count) {
    if (file_size < 22u) return false;
    const uint64_t window = file_size < kEocdWindow ? file_size : kEocdWindow;
    const uint64_t window_start = file_size - window;

    uint8_t* buffer = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(window)));
    if (buffer == nullptr) return false;
    if (!read_exact(fd, buffer, static_cast<size_t>(window), window_start)) {
        std::free(buffer);
        return false;
    }

    int64_t eocd = -1;
    for (int64_t i = static_cast<int64_t>(window) - 22; i >= 0; --i) {
        if (le32(buffer + i) == kSigEocd) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        LOGE("obb: no end-of-central-directory record in the APK");
        std::free(buffer);
        return false;
    }

    const uint8_t* record = buffer + eocd;
    uint64_t entries = le16(record + 10);
    uint64_t size = le32(record + 12);
    uint64_t offset = le32(record + 16);

    // zip64: the 32-bit fields are saturated and the real values live in the
    // zip64 record pointed at by the locator directly in front of the EOCD.
    if (entries == 0xffffu || size == kZip32Sentinel || offset == kZip32Sentinel) {
        bool resolved = false;
        if (eocd >= 20 && le32(record - 20) == kSigZip64Locator) {
            const uint64_t zip64_offset = le64(record - 20 + 8);
            uint8_t zip64[56];
            if (read_exact(fd, zip64, sizeof(zip64), zip64_offset) &&
                le32(zip64) == kSigZip64Eocd) {
                entries = le64(zip64 + 32);
                size = le64(zip64 + 40);
                offset = le64(zip64 + 48);
                resolved = true;
            }
        }
        if (!resolved) {
            LOGE("obb: the APK needs a zip64 central directory but none was found");
            std::free(buffer);
            return false;
        }
    }
    std::free(buffer);

    if (size == 0u || offset + size > file_size || size > kCentralDirectoryCap) {
        LOGE("obb: implausible central directory (offset=%" PRIu64 ", size=%" PRIu64
             ", apk=%" PRIu64 ")",
             offset, size, file_size);
        return false;
    }
    *cd_offset = offset;
    *cd_size = size;
    *entry_count = entries;
    return true;
}

// Only the fields that were saturated in the 32-bit header are present in the
// zip64 extra field, and always in this order.
inline void apply_zip64_extra(const uint8_t* extra, size_t extra_length,
                              bool need_uncompressed, bool need_compressed,
                              bool need_offset, ZipEntry* entry) {
    size_t cursor = 0u;
    while (cursor + 4u <= extra_length) {
        const uint16_t id = le16(extra + cursor);
        const uint16_t size = le16(extra + cursor + 2);
        const size_t body = cursor + 4u;
        if (body + size > extra_length) return;
        if (id == 0x0001u) {
            size_t field = body;
            if (need_uncompressed && field + 8u <= body + size) {
                entry->uncompressed = le64(extra + field);
                field += 8u;
            }
            if (need_compressed && field + 8u <= body + size) {
                entry->compressed = le64(extra + field);
                field += 8u;
            }
            if (need_offset && field + 8u <= body + size) {
                entry->local_offset = le64(extra + field);
            }
            return;
        }
        cursor = body + size;
    }
}

// assets/obb/*.obb is the documented place for the payload; a stray .obb
// anywhere else under assets/ is accepted with a lower score so a repack that
// dropped the file at assets/ still works.
inline int payload_score(const char* name) {
    if (std::strncmp(name, "assets/", 7) != 0) return 0;
    if (!ends_with_ci(name, ".obb")) return 0;
    int score = 1;
    if (std::strncmp(name, "assets/obb/", 11) == 0) score += 2;
    if (starts_with_ci(base_name_of(name), "main")) score += 1;
    return score;
}

inline bool scan_catalogue(int fd, uint64_t cd_offset, uint64_t cd_size,
                           uint64_t entry_count, Catalogue* catalogue) {
    uint8_t* directory = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(cd_size)));
    if (directory == nullptr) {
        LOGE("obb: cannot allocate %" PRIu64 " bytes for the central directory", cd_size);
        return false;
    }
    if (!read_exact(fd, directory, static_cast<size_t>(cd_size), cd_offset)) {
        LOGE("obb: cannot read the central directory: %s", std::strerror(errno));
        std::free(directory);
        return false;
    }

    size_t cursor = 0u;
    uint64_t seen = 0u;
    while (cursor + 46u <= static_cast<size_t>(cd_size) &&
           (entry_count == 0u || seen < entry_count)) {
        const uint8_t* header = directory + cursor;
        if (le32(header) != kSigCentral) break;

        const uint16_t method = le16(header + 10);
        const uint32_t crc = le32(header + 16);
        const uint32_t compressed32 = le32(header + 20);
        const uint32_t uncompressed32 = le32(header + 24);
        const uint16_t name_length = le16(header + 28);
        const uint16_t extra_length = le16(header + 30);
        const uint16_t comment_length = le16(header + 32);
        const uint32_t local32 = le32(header + 42);
        const size_t record = 46u + name_length + extra_length + comment_length;
        if (cursor + record > static_cast<size_t>(cd_size)) break;

        if (name_length < kNameCap) {
            ZipEntry entry {};
            std::memcpy(entry.name, header + 46, name_length);
            entry.name[name_length] = '\0';
            entry.method = method;
            entry.crc = crc;
            entry.compressed = compressed32;
            entry.uncompressed = uncompressed32;
            entry.local_offset = local32;
            entry.valid = true;
            if (compressed32 == kZip32Sentinel || uncompressed32 == kZip32Sentinel ||
                local32 == kZip32Sentinel) {
                apply_zip64_extra(header + 46 + name_length, extra_length,
                                  uncompressed32 == kZip32Sentinel,
                                  compressed32 == kZip32Sentinel,
                                  local32 == kZip32Sentinel, &entry);
            }

            if (std::strcmp(entry.name, "AndroidManifest.xml") == 0) {
                catalogue->manifest = entry;
            } else {
                const int score = payload_score(entry.name);
                if (score > 0) {
                    const bool is_patch = starts_with_ci(base_name_of(entry.name), "patch");
                    if (is_patch) {
                        if (score > catalogue->patch_score) {
                            catalogue->patch_payload = entry;
                            catalogue->patch_score = score;
                        }
                    } else if (score > catalogue->main_score) {
                        catalogue->main_payload = entry;
                        catalogue->main_score = score;
                    }
                }
            }
        }

        cursor += record;
        ++seen;
    }
    std::free(directory);
    return true;
}

inline bool entry_data_offset(int fd, const ZipEntry& entry, uint64_t* data_offset) {
    uint8_t local[30];
    if (!read_exact(fd, local, sizeof(local), entry.local_offset)) return false;
    if (le32(local) != kSigLocal) return false;
    *data_offset = entry.local_offset + 30u + le16(local + 26) + le16(local + 28);
    return true;
}

// --- binary AndroidManifest.xml -------------------------------------------
//
// Only what is needed for two attributes of the <manifest> element. Attribute
// names are matched as strings, so no resource-id table is required.

struct StringPool {
    const uint8_t* chunk;
    size_t chunk_size;
    const uint8_t* offsets;
    const uint8_t* strings;
    uint32_t count;
    bool utf8;
};

inline bool pool_string(const StringPool& pool, uint32_t index, char* out, size_t capacity) {
    if (capacity == 0u) return false;
    out[0] = '\0';
    if (pool.chunk == nullptr || index >= pool.count) return false;

    const size_t offset = le32(pool.offsets + 4u * index);
    const uint8_t* end = pool.chunk + pool.chunk_size;
    const uint8_t* cursor = pool.strings + offset;
    if (cursor + 2 > end) return false;

    size_t written = 0u;
    if (pool.utf8) {
        uint32_t utf16_units = cursor[0];
        ++cursor;
        if ((utf16_units & 0x80u) != 0u) {
            if (cursor >= end) return false;
            utf16_units = ((utf16_units & 0x7fu) << 8) | cursor[0];
            ++cursor;
        }
        if (cursor >= end) return false;
        uint32_t bytes = cursor[0];
        ++cursor;
        if ((bytes & 0x80u) != 0u) {
            if (cursor >= end) return false;
            bytes = ((bytes & 0x7fu) << 8) | cursor[0];
            ++cursor;
        }
        for (uint32_t i = 0u; i < bytes && cursor + i < end && written + 1u < capacity; ++i) {
            out[written++] = static_cast<char>(cursor[i]);
        }
    } else {
        uint32_t units = le16(cursor);
        cursor += 2;
        if ((units & 0x8000u) != 0u) {
            if (cursor + 2 > end) return false;
            units = ((units & 0x7fffu) << 16) | le16(cursor);
            cursor += 2;
        }
        for (uint32_t i = 0u; i < units && cursor + 2u * i + 2u <= end && written + 1u < capacity; ++i) {
            const uint16_t unit = le16(cursor + 2u * i);
            out[written++] = (unit != 0u && unit < 0x80u) ? static_cast<char>(unit) : '?';
        }
    }
    out[written] = '\0';
    return true;
}

inline bool parse_manifest(const uint8_t* data, size_t size, char* package,
                           size_t package_capacity, int64_t* version_code) {
    if (size < 8u || le16(data) != 0x0003u) return false;

    StringPool pool {};
    size_t cursor = 8u;
    bool found = false;
    while (cursor + 8u <= size) {
        const uint8_t* chunk = data + cursor;
        const uint16_t type = le16(chunk);
        const uint16_t header_size = le16(chunk + 2);
        const uint32_t chunk_size = le32(chunk + 4);
        if (chunk_size < 8u || header_size < 8u ||
            cursor + chunk_size > size || header_size > chunk_size) {
            break;
        }

        if (type == 0x0001u && chunk_size >= 28u) { // RES_STRING_POOL_TYPE
            const uint32_t flags = le32(chunk + 16);
            pool.chunk = chunk;
            pool.chunk_size = chunk_size;
            pool.count = le32(chunk + 8);
            pool.offsets = chunk + header_size;
            pool.strings = chunk + le32(chunk + 20);
            pool.utf8 = (flags & 0x0100u) != 0u;
            if (pool.strings < chunk || pool.strings > chunk + chunk_size ||
                pool.offsets + 4u * static_cast<size_t>(pool.count) > chunk + chunk_size) {
                pool.chunk = nullptr;
            }
        } else if (type == 0x0102u && pool.chunk != nullptr) { // RES_XML_START_ELEMENT
            const uint8_t* extension = chunk + header_size;
            if (extension + 20u > chunk + chunk_size) break;
            char element[64];
            if (!pool_string(pool, le32(extension + 4), element, sizeof(element))) break;
            if (std::strcmp(element, "manifest") != 0) {
                cursor += chunk_size;
                continue;
            }

            const uint16_t attribute_start = le16(extension + 8);
            const uint16_t attribute_size = le16(extension + 10);
            const uint16_t attribute_count = le16(extension + 12);
            for (uint16_t i = 0u; i < attribute_count && attribute_size >= 20u; ++i) {
                const uint8_t* attribute = extension + attribute_start +
                                           static_cast<size_t>(i) * attribute_size;
                if (attribute + 20u > chunk + chunk_size) break;
                char name[64];
                if (!pool_string(pool, le32(attribute + 4), name, sizeof(name))) continue;
                const uint32_t raw_value = le32(attribute + 8);
                const uint8_t value_type = attribute[15];
                const uint32_t value = le32(attribute + 16);

                if (std::strcmp(name, "package") == 0) {
                    pool_string(pool, raw_value, package, package_capacity);
                } else if (std::strcmp(name, "versionCode") == 0) {
                    if (value_type == 0x10u) { // TYPE_INT_DEC
                        *version_code = static_cast<int64_t>(static_cast<int32_t>(value));
                    } else if (raw_value != kZip32Sentinel) {
                        char text[32];
                        if (pool_string(pool, raw_value, text, sizeof(text))) {
                            *version_code = std::strtoll(text, nullptr, 10);
                        }
                    }
                }
            }
            found = true;
            break;
        }
        cursor += chunk_size;
    }
    return found;
}

// --- extraction ------------------------------------------------------------

inline bool read_entry_to_memory(int fd, const ZipEntry& entry, uint64_t data_offset,
                                 uint8_t* out, size_t capacity, size_t* produced) {
    if (entry.uncompressed == 0u || entry.uncompressed > capacity) return false;

    if (entry.method == kMethodStore) {
        if (!read_exact(fd, out, static_cast<size_t>(entry.uncompressed), data_offset)) return false;
        *produced = static_cast<size_t>(entry.uncompressed);
        return true;
    }
    if (entry.method != kMethodDeflate) return false;

    uint8_t* input = static_cast<uint8_t*>(std::malloc(kChunk));
    if (input == nullptr) return false;

    z_stream stream {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        std::free(input);
        return false;
    }
    stream.next_out = out;
    stream.avail_out = static_cast<uInt>(capacity);

    bool ok = true;
    uint64_t remaining = entry.compressed;
    while (ok && remaining > 0u) {
        const size_t take = remaining < kChunk ? static_cast<size_t>(remaining) : kChunk;
        if (!read_exact(fd, input, take, data_offset + (entry.compressed - remaining))) {
            ok = false;
            break;
        }
        remaining -= take;
        stream.next_in = input;
        stream.avail_in = static_cast<uInt>(take);
        const int status = inflate(&stream, Z_NO_FLUSH);
        if (status == Z_STREAM_END) break;
        if (status != Z_OK && status != Z_BUF_ERROR) ok = false;
    }
    *produced = capacity - stream.avail_out;
    inflateEnd(&stream);
    std::free(input);
    return ok && *produced > 0u;
}

inline bool extract_entry_to_file(int fd, const ZipEntry& entry, uint64_t data_offset,
                                  const char* temporary) {
    const int out = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) {
        LOGE("obb: cannot create '%s': %s", temporary, std::strerror(errno));
        return false;
    }

    uint8_t* input = static_cast<uint8_t*>(std::malloc(kChunk));
    uint8_t* output = static_cast<uint8_t*>(std::malloc(kChunk));
    if (input == nullptr || output == nullptr) {
        LOGE("obb: out of memory while preparing the copy");
        std::free(input);
        std::free(output);
        close(out);
        unlink(temporary);
        return false;
    }

    const bool inflating = entry.method == kMethodDeflate;
    z_stream stream {};
    if (inflating && inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        LOGE("obb: inflateInit2 failed for '%s'", entry.name);
        std::free(input);
        std::free(output);
        close(out);
        unlink(temporary);
        return false;
    }

    bool ok = true;
    bool finished = false;
    uint64_t remaining = entry.compressed;
    uint64_t written = 0u;
    uint64_t logged = 0u;
    uLong crc = crc32(0uL, Z_NULL, 0);

    while (ok && !finished && remaining > 0u) {
        const size_t take = remaining < kChunk ? static_cast<size_t>(remaining) : kChunk;
        if (!read_exact(fd, input, take, data_offset + (entry.compressed - remaining))) {
            LOGE("obb: read error in the APK at %" PRIu64 ": %s",
                 data_offset + (entry.compressed - remaining), std::strerror(errno));
            ok = false;
            break;
        }
        remaining -= take;

        if (!inflating) {
            if (!write_exact(out, input, take)) {
                LOGE("obb: write error: %s", std::strerror(errno));
                ok = false;
                break;
            }
            crc = crc32(crc, input, static_cast<uInt>(take));
            written += take;
        } else {
            stream.next_in = input;
            stream.avail_in = static_cast<uInt>(take);
            while (ok && stream.avail_in > 0u) {
                stream.next_out = output;
                stream.avail_out = static_cast<uInt>(kChunk);
                const int status = inflate(&stream, Z_NO_FLUSH);
                if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
                    LOGE("obb: inflate failed for '%s' (zlib %d)", entry.name, status);
                    ok = false;
                    break;
                }
                const size_t produced = kChunk - stream.avail_out;
                if (produced > 0u) {
                    if (!write_exact(out, output, produced)) {
                        LOGE("obb: write error: %s", std::strerror(errno));
                        ok = false;
                        break;
                    }
                    crc = crc32(crc, output, static_cast<uInt>(produced));
                    written += produced;
                }
                if (status == Z_STREAM_END) {
                    finished = true;
                    break;
                }
                if (produced == 0u) break;
            }
        }

        if (ok && entry.uncompressed > 0u && written - logged >= kProgressStep) {
            logged = written;
            LOGI("obb: copying '%s' — %" PRIu64 "/%" PRIu64 " bytes (%" PRIu64 "%%)",
                 base_name_of(entry.name), written, entry.uncompressed,
                 written * 100u / entry.uncompressed);
        }
    }

    if (inflating) inflateEnd(&stream);
    std::free(input);
    std::free(output);

    if (ok && written != entry.uncompressed) {
        LOGE("obb: '%s' produced %" PRIu64 " bytes but the APK says %" PRIu64,
             entry.name, written, entry.uncompressed);
        ok = false;
    }
    if (ok && entry.crc != 0u && static_cast<uint32_t>(crc) != entry.crc) {
        LOGE("obb: CRC-32 mismatch for '%s' (got 0x%08" PRIx32 ", expected 0x%08" PRIx32 ")",
             entry.name, static_cast<uint32_t>(crc), entry.crc);
        ok = false;
    }
    if (ok && fsync(out) != 0) {
        LOGE("obb: fsync failed: %s", std::strerror(errno));
        ok = false;
    }
    close(out);

    if (!ok) {
        unlink(temporary);
        return false;
    }
    // The game opens the file with its own uid, so 0644 is enough; on FUSE
    // emulated storage the mode is fixed by the volume and chmod is a no-op.
    if (chmod(temporary, 0644) != 0 && errno != EPERM) {
        LOGW("obb: chmod 0644 on '%s' failed: %s", temporary, std::strerror(errno));
    }
    return true;
}

// --- destination -----------------------------------------------------------

inline bool ensure_directory(const char* path) {
    struct stat64 info {};
    if (stat64(path, &info) == 0) return S_ISDIR(info.st_mode);
    if (mkdir(path, 0755) == 0) {
        if (chmod(path, 0755) != 0 && errno != EPERM) {
            LOGW("obb: chmod 0755 on '%s' failed: %s", path, std::strerror(errno));
        }
        return true;
    }
    if (errno == EEXIST) return stat64(path, &info) == 0 && S_ISDIR(info.st_mode);
    return false;
}

inline size_t external_roots(char roots[3][kPathCap]) {
    size_t count = 0u;
    const char* environment = getenv("EXTERNAL_STORAGE");
    if (environment != nullptr && environment[0] == '/') {
        std::snprintf(roots[count++], kPathCap, "%s", environment);
    }
    const char* defaults[2] = { "/storage/emulated/0", "/sdcard" };
    for (size_t i = 0u; i < 2u && count < 3u; ++i) {
        bool duplicate = false;
        for (size_t j = 0u; j < count; ++j) {
            if (std::strcmp(roots[j], defaults[i]) == 0) duplicate = true;
        }
        if (!duplicate) std::snprintf(roots[count++], kPathCap, "%s", defaults[i]);
    }
    return count;
}

inline bool free_space_available(const char* directory, uint64_t needed) {
    struct statvfs stats {};
    if (statvfs(directory, &stats) != 0) return true; // unknown: let the copy decide
    const uint64_t free_bytes = static_cast<uint64_t>(stats.f_bavail) *
                                static_cast<uint64_t>(stats.f_frsize);
    if (free_bytes >= needed + kFreeSpaceMargin) return true;
    LOGE("obb: not enough free space on '%s': %" PRIu64 " MiB available, %" PRIu64
         " MiB needed",
         directory, free_bytes / (1024u * 1024u),
         (needed + kFreeSpaceMargin) / (1024u * 1024u));
    return false;
}

inline bool place_payload(int apk_fd, const ZipEntry& entry, const char* kind,
                          const char* package, int64_t version_code) {
    char roots[3][kPathCap];
    const size_t root_count = external_roots(roots);

    char directory[kPathCap];
    directory[0] = '\0';
    for (size_t i = 0u; i < root_count && directory[0] == '\0'; ++i) {
        char obb_root[kPathCap];
        std::snprintf(obb_root, sizeof(obb_root), "%s/Android/obb", roots[i]);
        if (!ensure_directory(obb_root)) {
            LOGW("obb: '%s' unusable: %s", obb_root, std::strerror(errno));
            continue;
        }
        char candidate[kPathCap];
        std::snprintf(candidate, sizeof(candidate), "%s/%s", obb_root, package);
        if (!ensure_directory(candidate)) {
            LOGW("obb: cannot create '%s': %s", candidate, std::strerror(errno));
            continue;
        }
        std::snprintf(directory, sizeof(directory), "%s", candidate);
    }
    if (directory[0] == '\0') {
        LOGE("obb: no writable Android/obb directory for '%s'; the expansion file "
             "was NOT provisioned", package);
        return false;
    }

    char destination[kPathCap];
    std::snprintf(destination, sizeof(destination), "%s/%s.%" PRId64 ".%s.obb",
                  directory, kind, version_code, package);

    struct stat64 existing {};
    if (stat64(destination, &existing) == 0) {
        if (static_cast<uint64_t>(existing.st_size) == entry.uncompressed) {
            if (chmod(destination, 0644) != 0 && errno != EPERM) {
                LOGW("obb: chmod 0644 on '%s' failed: %s", destination, std::strerror(errno));
            }
            LOGI("obb: '%s' already in place (%" PRIu64 " bytes); nothing to copy",
                 destination, entry.uncompressed);
            return true;
        }
        LOGW("obb: '%s' is %" PRId64 " bytes but the APK carries %" PRIu64
             "; replacing it",
             destination, static_cast<int64_t>(existing.st_size), entry.uncompressed);
    }

    if (!free_space_available(directory, entry.uncompressed)) return false;

    uint64_t data_offset = 0u;
    if (!entry_data_offset(apk_fd, entry, data_offset ? data_offset : 0u)) {
        // placeholder, replaced below
    }
    if (!entry_data_offset(apk_fd, entry, &data_offset)) {
        LOGE("obb: broken local header for '%s'", entry.name);
        return false;
    }

    LOGI("obb: extracting '%s' (%" PRIu64 " bytes, %s) to '%s'",
         entry.name, entry.uncompressed,
         entry.method == kMethodStore ? "stored" : "deflated", destination);

    char temporary[kPathCap];
    std::snprintf(temporary, sizeof(temporary), "%s.opg3d-part", destination);
    const uint64_t started = ::opg3d_log::monotonic_ms();
    if (!extract_entry