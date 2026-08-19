#include "il2cpp.h"

#include <dlfcn.h>
#include <cstring>

#include "log.h"

namespace il2cpp {
namespace {

template <typename T>
bool bind(T& fn, const char* name, bool required) {
    if (fn != nullptr) return true;  // уже привязан (resolve() зовём в цикле ожидания)
    fn = reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
    if (fn == nullptr && required) {
        LOGW("il2cpp: символ %s пока недоступен", name);
    }
    return fn != nullptr;
}

} // namespace

bool resolve() {
    bool ok = true;
    ok &= bind(string_new,               "il2cpp_string_new",               true);
    ok &= bind(domain_get,               "il2cpp_domain_get",               true);
    ok &= bind(domain_get_assemblies,    "il2cpp_domain_get_assemblies",    true);
    ok &= bind(assembly_get_image,       "il2cpp_assembly_get_image",       true);
    ok &= bind(image_get_name,           "il2cpp_image_get_name",           true);
    ok &= bind(class_from_name,          "il2cpp_class_from_name",          true);
    ok &= bind(class_get_field_from_name,"il2cpp_class_get_field_from_name",true);
    ok &= bind(field_static_get_value,   "il2cpp_field_static_get_value",   true);
    bind(runtime_class_init,  "il2cpp_runtime_class_init",  false);
    bind(domain_assembly_open,"il2cpp_domain_assembly_open",false);
    return ok;
}

void* find_image(const char* name) {
    if (!domain_get || !domain_get_assemblies || !assembly_get_image || !image_get_name) {
        return nullptr;
    }
    void* domain = domain_get();
    if (!domain) return nullptr;

    size_t count = 0;
    void** assemblies = domain_get_assemblies(domain, &count);
    if (!assemblies) return nullptr;

    for (size_t i = 0; i < count; ++i) {
        void* image = assembly_get_image(assemblies[i]);
        if (!image) continue;
        const char* n = image_get_name(image);
        if (n && std::strcmp(n, name) == 0) return image;
    }
    return nullptr;
}

} // namespace il2cpp
