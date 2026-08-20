#include "il2cpp.h"

#include <algorithm>
#include <cstring>

#include "elf_sym.h"

namespace il2cpp {
namespace {

constexpr const char* kSo = "libil2cpp.so";

// Вменяемый потолок для количества managed-сборок. Если рантайм отдал больше,
// значит мы прочитали вектор в момент перевыделения — обходить его нельзя.
constexpr size_t kMaxAssemblies = 8192u;

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

void* find_class(const char* namespaze, const char* name) {
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

    // Ищем во всех managed-сборках: старые PUN-плагины могли быть собраны как
    // Assembly-CSharp, Assembly-CSharp-firstpass или отдельный Photon assembly.
    for (size_t i = 0; i < count; ++i) {
        if (assemblies[i] == nullptr) continue;
        void* image = assembly_get_image(assemblies[i]);
        if (image == nullptr) continue;
        void* klass = class_from_name(image, namespaze, name);
        if (klass != nullptr) return klass;
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
    // В metadata v22 первый элемент MethodInfo — Il2CppMethodPointer.
    // memcpy избегает нарушений strict-aliasing.
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
