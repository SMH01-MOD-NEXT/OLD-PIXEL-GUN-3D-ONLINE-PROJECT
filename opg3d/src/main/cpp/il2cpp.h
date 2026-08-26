#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

// Minimal wrapper over the exported IL2CPP C API of PG3D 13.2.1
// (metadata v22 — same major version as 12.5.0).
// Symbols are resolved directly from the already loaded libil2cpp.so: the
// Android linker namespace is not required to make them visible to dlsym().
namespace il2cpp {

inline void*       (*string_new)(const char* str) = nullptr;
inline int32_t     (*string_length)(void* str) = nullptr;
inline const uint16_t* (*string_chars)(void* str) = nullptr;
inline void*       (*domain_get)() = nullptr;
inline void**      (*domain_get_assemblies)(void* domain, size_t* count) = nullptr;
inline void*       (*assembly_get_image)(void* assembly) = nullptr;
inline const char* (*image_get_name)(void* image) = nullptr;
inline void*       (*class_from_name)(void* image, const char* namespaze,
                                      const char* name) = nullptr;
inline void*       (*class_get_method_from_name)(void* klass, const char* name,
                                                 int args_count) = nullptr;
inline void*       (*class_get_field_from_name)(void* klass, const char* name) = nullptr;

// Nested-type walk, used to reach compiler-generated iterator state machines.
// il2cpp_class_get_nested_types() is an iterator export: `iter` must start out
// as nullptr, the runtime advances it on every call, and the call returns
// nullptr once the list is exhausted. Both symbols are bound outside the
// required set (see resolve()), so a layout without them still gets every
// top-level target hooked.
inline void*       (*class_get_nested_types)(void* klass, void** iter) = nullptr;
inline const char* (*class_get_name)(void* klass) = nullptr;

inline void*       (*object_get_class)(void* object) = nullptr;
inline void        (*field_get_value)(void* object, void* field, void* value) = nullptr;

// Managed allocation and reflection Type lookup.
//
// These three are bound outside the required set (see resolve()), because only
// the PixelPass season path needs them: a layout without them still installs
// every other hook instead of failing the whole library.
//
// il2cpp_object_new allocates a zeroed instance and does NOT run any
// constructor -- the .ctor has to be invoked separately, exactly the way the
// `newobj` IL instruction does it. Calling a managed .ctor on an object that
// was never allocated by the runtime would corrupt the heap, so these two
// always travel together.
//
// class_get_type + type_get_object turn an Il2CppClass* into the managed
// System.Type object that reflection-shaped APIs such as
// JsonConvert.DeserializeObject(string, Type) expect.
inline void*       (*object_new)(void* klass) = nullptr;
inline const void* (*class_get_type)(void* klass) = nullptr;
inline void*       (*type_get_object)(const void* type) = nullptr;

// In the old embedding IL2CPP API the third argument of
// il2cpp_field_set_value has an asymmetric ABI: for value types it is a
// pointer to the value, while for managed references (string/class/object/
// array) it IS the object pointer itself.
//
// Our write_field<T>() passes the address of a local T. This adapter keeps
// passing the address for numbers/enums/bools, but strips one level of
// indirection when T is itself a pointer. Without it, a string field would be
// written with the address of a native stack local: the reference goes stale
// after return, and the next heap walk crashes inside the GC.
struct FieldSetValueApi {
    using RawFn = void (*)(void* object, void* field, void* value);
    RawFn raw = nullptr;

    explicit operator bool() const noexcept { return raw != nullptr; }
    bool operator==(std::nullptr_t) const noexcept { return raw == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return raw != nullptr; }

    template <typename T>
    void operator()(void* object, void* field, T* value) const noexcept {
        if (raw == nullptr || value == nullptr) return;
        if constexpr (std::is_pointer_v<T>) {
            raw(object, field,
                const_cast<void*>(static_cast<const void*>(*value)));
        } else {
            raw(object, field, value);
        }
    }
};
inline FieldSetValueApi field_set_value{};

inline void        (*field_static_get_value)(void* field, void* value) = nullptr;
inline void        (*field_static_set_value)(void* field, void* value) = nullptr;
inline void*       (*thread_attach)(void* domain) = nullptr;
inline void        (*thread_detach)(void* thread) = nullptr;

// Resolves the required set of exports. Idempotent.
bool resolve();

// Metadata lookup without running managed static constructors.
void* find_image(const char* name);

// Resolves a class by namespace and name across every loaded assembly.
//
// Compiler-generated iterator state machines (`<Method>c__IteratorN`) are
// nested types, and in metadata v22 a nested type is reachable only through
// its declaring type: the image-level type list that il2cpp_class_from_name()
// walks holds top-level types only. That is why no spelling of a composite
// name — "Outer/Nested", "Outer.Nested", or the bare "Nested" — can ever be
// resolved by that export on its own.
//
// So when the direct lookup fails and the name is composite, the name is split
// at '/' or '.' (from the right, because '.' is also the namespace separator),
// the left part is resolved recursively as the declaring type, and that type's
// nested types are matched by their short metadata name.
void* find_class(const char* namespaze, const char* name);

// Returns the nested type of `outer` whose short metadata name is exactly
// `name` (for example "<SendCheatTypeOnServer>c__Iterator0"), or nullptr.
void* find_nested_class(void* outer, const char* name);

void* find_method_info(const char* namespaze, const char* klass,
                       const char* method, int args_count);
void* method_pointer(void* method_info);
void* find_field(const char* namespaze, const char* klass, const char* field);

// Safe diagnostic conversion of a managed UTF-16 string.
// max_code_units caps logcat message size.
std::string to_utf8(void* managed_string, size_t max_code_units = 512);

} // namespace il2cpp
