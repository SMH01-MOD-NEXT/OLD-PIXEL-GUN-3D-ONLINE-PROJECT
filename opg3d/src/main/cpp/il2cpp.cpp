#include "il2cpp.h"

#include <cstring>

#include "elf_sym.h"

namespace il2cpp {
namespace {

constexpr const char* kSo = "libil2cpp.so";

template <typename T>
bool bind(T& fn, const char* name) {
    if (fn != nullptr) return true;  // уже привязан (resolve() зовём в цикле ожидания)
    fn = reinterpret_cast<T>(elfsym::find_symbol(kSo, name));
    return fn != nullptr;
}

} // namespace

bool resolve() {
    bool ok = true;
    ok &= bind(string_new,                "il2cpp_string_new");
    ok &= bind(domain_get,                "il2cpp_domain_get");
    ok &= bind(domain_get_assemblies,     "il2cpp_domain_get_assemblies");
    ok &= bind(assembly_get_image,        "il2cpp_assembly_get_image");
    ok &= bind(image_get_name,            "il2cpp_image_get_name");
    ok &= bind(class_from_name,           "il2cpp_class_from_name");
    ok &= bind(class_get_field_from_name, "il2cpp_class_get_field_from_name");
    ok &= bind(field_static_get_value,    "il2cpp_field_static_get_value");
    ok &= bind(thread_attach,             "il2cpp_thread_attach");
    bind(thread_detach, "il2cpp_thread_detach");  // опционально
    return ok;
}

void* find_image(const char* name) {
    if (!domain_get || !domain_get_assemblies || !assembly_get_image || !image_get_name) {
        return nullptr;
    }
    void* domain = domain_get();
    if (domain == nullptr) return nullptr;

    size_t count = 0;
    void** assemblies = domain_get_assemblies(domain, &count);
    if (assemblies == nullptr) return nullptr;

    for (size_t i = 0; i < count; ++i) {
        void* image = assembly_get_image(assemblies[i]);
        if (image == nullptr) continue;
        const char* n = image_get_name(image);
        if (n != nullptr && std::strcmp(n, name) == 0) return image;
    }
    return nullptr;
}

} // namespace il2cpp
