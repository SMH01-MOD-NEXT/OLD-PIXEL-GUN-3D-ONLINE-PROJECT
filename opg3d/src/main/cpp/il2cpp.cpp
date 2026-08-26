#include "il2cpp.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "elf_sym.h"

namespace il2cpp {
namespace {

constexpr const char* kSo = "libil2cpp.so";

// A sane upper bound for the number of managed assemblies. If the runtime
// reports more, we have read the vector mid-reallocation and must not walk it.
constexpr size_t kMaxAssemblies = 8192u;

// Upper bound for one nested-type walk. A compiler-generated class family
// never comes close; the cap only guarantees termination if the runtime ever
// hands back an inconsistent iterator.
constexpr size_t kMaxNestedTypes = 4096u;

template <typename T>
bool bind(T& fn, const char* name) {
    if (fn != nullptr) return true;
    fn = reinterpret_cast<T>(elfsym::find_symbol(kSo, name));
    return fn != nullptr;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6u)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12u)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18u)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

// Exactly what il2cpp_class_from_name() can see: the top-level type list of
// each loaded image. Nested types are deliberately not reachable here.
void* class_from_name_any_image(const char* namespaze, const char* name) {
    if (!domain_get || !domain_get_assemblies || !assembly_get_image ||
        !class_from_name || namespaze == nullptr || name == nullptr) {
        return nullptr;
    }

    void* domain = domain_get();
    if (domain == nullptr) return nullptr;

    size_t count = 0;
    void** assemblies = domain_get_assemblies(domain, &count);
    if (assemblies == nullptr || count == 0u || count > kMaxAssemblies) {
        return nullptr;
    }

    // Search all managed assemblies: old PUN plugins could be built as
    // Assembly-CSharp, Assembly-CSharp-firstpass or a separate Photon assembly.
    for (size_t i = 0; i < count; ++i) {
        if (assemblies[i] == nullptr) continue;
        void* image = assembly_get_image(assemblies[i]);
        if (image == nullptr) continue;
        void* klass = class_from_name(image, namespaze, name);
        if (klass != nullptr) return klass;
    }
    return nullptr;
}

} // namespace

bool resolve() {
    bool ok = true;
    ok &= bind(string_new,                "il2cpp_string_new");
    ok &= bind(string_length,             "il2cpp_string_length");
    ok &= bind(string_chars,              "il2cpp_string_chars");
    ok &= bind(domain_get,                "il2cpp_domain_get");
    ok &= bind(domain_get_assemblies,     "il2cpp_domain_get_assemblies");
    ok &= bind(assembly_get_image,        "il2cpp_assembly_get_image");
    ok &= bind(image_get_name,            "il2cpp_image_get_name");
    ok &= bind(class_from_name,           "il2cpp_class_from_name");
    ok &= bind(class_get_method_from_name,"il2cpp_class_get_method_from_name");
    ok &= bind(class_get_field_from_name, "il2cpp_class_get_field_from_name");
    ok &= bind(object_get_class,          "il2cpp_object_get_class");
    ok &= bind(field_get_value,           "il2cpp_field_get_value");
    ok &= bind(field_set_value.raw,       "il2cpp_field_set_value");
    ok &= bind(field_static_get_value,    "il2cpp_field_static_get_value");
    ok &= bind(field_static_set_value,    "il2cpp_field_static_set_value");
    ok &= bind(thread_attach,             "il2cpp_thread_attach");
    bind(thread_detach, "il2cpp_thread_detach");

    // Optional on purpose: only nested-type resolution needs these two. Keeping
    // them out of `ok` means a layout that lacks them still installs every
    // top-level hook instead of failing the whole module; the nested iterator
    // blocks degrade to the same "not found" warning they produced before.
    bind(class_get_nested_types, "il2cpp_class_get_nested_types");
    bind(class_get_name,         "il2cpp_class_get_name");

    // Optional for the same reason: only the PixelPass season construction
    // path allocates a managed object or needs a reflection Type. Every caller
    // checks these for null before use, so a layout without them loses the
    // battle pass and nothing else.
    bind(object_new,      "il2cpp_object_new");
    bind(class_get_type,  "il2cpp_class_get_type");
    bind(type_get_object, "il2cpp_type_get_object");
    return ok;
}

void* find_image(const char* name) {
    if (!domain_get || !domain_get_assemblies || !assembly_get_image ||
        !image_get_name || name == nullptr) {
        return nullptr;
    }

    void* domain = domain_get();
    if (domain == nullptr) return nullptr;

    size_t count = 0;
    void** assemblies = domain_get_assemblies(domain, &count);
    if (assemblies == nullptr || count == 0u || count > kMaxAssemblies) {
        return nullptr;
    }

    for (size_t i = 0; i < count; ++i) {
        if (assemblies[i] == nullptr) continue;
        void* image = assembly_get_image(assemblies[i]);
        if (image == nullptr) continue;
        const char* image_name = image_get_name(image);
        if (image_name != nullptr && std::strcmp(image_name, name) == 0) {
            return image;
        }
    }
    return nullptr;
}

void* find_nested_class(void* outer, const char* name) {
    if (outer == nullptr || name == nullptr ||
        class_get_nested_types == nullptr || class_get_name == nullptr) {
        return nullptr;
    }

    // GetNestedTypes() sets up the nested-type list of the declaring class on
    // demand. It does not run managed static constructors, so this keeps the
    // same contract as the rest of the lookups in this file.
    void* iter = nullptr;
    for (size_t guard = 0; guard < kMaxNestedTypes; ++guard) {
        void* nested = class_get_nested_types(outer, &iter);
        if (nested == nullptr) break;
        const char* nested_name = class_get_name(nested);
        if (nested_name != nullptr && std::strcmp(nested_name, name) == 0) {
            return nested;
        }
    }
    return nullptr;
}

void* find_class(const char* namespaze, const char* name) {
    if (namespaze == nullptr || name == nullptr) return nullptr;

    void* direct = class_from_name_any_image(namespaze, name);
    if (direct != nullptr) return direct;

    // Nested fallback (see the comment on find_class in il2cpp.h). Split points
    // are tried from the right, so "Outer/<M>c__Iterator0" and
    // "Outer.<M>c__Iterator0" both resolve, and the plain lookup above always
    // gets the first attempt at a dotted name that is really a namespace.
    const std::string full(name);
    for (size_t i = full.size(); i-- > 0;) {
        const char c = full[i];
        if (c != '/' && c != '.') continue;
        if (i == 0u || i + 1u >= full.size()) continue;

        // The declaring type may itself be nested, hence the recursion. It
        // always terminates: the outer name is strictly shorter every time.
        void* outer = find_class(namespaze, full.substr(0, i).c_str());
        if (outer == nullptr) continue;

        void* nested = find_nested_class(outer, full.substr(i + 1u).c_str());
        if (nested != nullptr) return nested;
    }
    return nullptr;
}

void* find_method_info(const char* namespaze, const char* klass,
                       const char* method, int args_count) {
    if (!class_get_method_from_name || method == nullptr) return nullptr;
    void* type = find_class(namespaze, klass);
    if (type == nullptr) return nullptr;
    return class_get_method_from_name(type, method, args_count);
}

void* method_pointer(void* method_info) {
    if (method_info == nullptr) return nullptr;
    // In metadata v22 the first member of MethodInfo is Il2CppMethodPointer.
    // memcpy avoids strict-aliasing violations.
    void* pointer = nullptr;
    std::memcpy(&pointer, method_info, sizeof(pointer));
    return pointer;
}

void* find_field(const char* namespaze, const char* klass, const char* field) {
    if (!class_get_field_from_name || field == nullptr) return nullptr;
    void* type = find_class(namespaze, klass);
    if (type == nullptr) return nullptr;
    return class_get_field_from_name(type, field);
}

std::string to_utf8(void* managed_string, size_t max_code_units) {
    if (managed_string == nullptr) return "<null>";
    if (!string_length || !string_chars) return "<string-api-unavailable>";

    const int32_t signed_length = string_length(managed_string);
    if (signed_length <= 0) return {};

    const uint16_t* chars = string_chars(managed_string);
    if (chars == nullptr) return "<null-chars>";

    const size_t length = static_cast<size_t>(signed_length);
    const size_t limit = std::min(length, max_code_units);
    std::string result;
    result.reserve(limit + 8u);

    for (size_t i = 0; i < limit; ++i) {
        uint32_t cp = chars[i];
        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1u < limit) {
            const uint32_t low = chars[i + 1u];
            if (low >= 0xDC00u && low <= 0xDFFFu) {
                cp = 0x10000u + ((cp - 0xD800u) << 10u) + (low - 0xDC00u);
                ++i;
            } else {
                cp = 0xFFFDu;
            }
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            cp = 0xFFFDu;
        }
        append_utf8(result, cp);
    }

    if (length > limit) result += "...";
    return result;
}

} // namespace il2cpp
