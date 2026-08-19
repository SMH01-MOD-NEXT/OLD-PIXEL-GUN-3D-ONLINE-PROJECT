#pragma once

#include <cstddef>

// Минимальный набор указателей на экспортируемый C-API IL2CPP.
//
// ВАЖНО: символы НЕ резолвим через dlsym(RTLD_DEFAULT) — игра грузит
// libil2cpp.so через System.loadLibrary (RTLD_LOCAL + линкерные namespace'ы
// Android 7+), поэтому в глобальном списке символов её нет. Вместо этого
// берём handle через shadowhook_dlopen() (ходит по solist линкера напрямую)
// и дёргаем символы из него.
namespace il2cpp {

inline void*       (*string_new)(const char* str) = nullptr;
inline void*       (*domain_get)() = nullptr;
inline void**      (*domain_get_assemblies)(void* domain, size_t* count) = nullptr;
inline void*       (*assembly_get_image)(void* assembly) = nullptr;
inline const char* (*image_get_name)(void* image) = nullptr;
inline void*       (*class_from_name)(void* image, const char* namespaze, const char* name) = nullptr;
inline void*       (*class_get_field_from_name)(void* klass, const char* name) = nullptr;
inline void        (*field_static_get_value)(void* field, void* value) = nullptr;
inline void        (*runtime_class_init)(void* klass) = nullptr;

// Резолвит символы из уже загруженной libil2cpp.so по handle (shadowhook_dlopen).
// Тихо (без логов на каждый символ). Идемпотентно. true = обязательный минимум на месте.
bool resolve(void* il2cpp_handle);

// Ищет image сборки по имени (например "Assembly-CSharp.dll"). nullptr, если не найден.
void* find_image(const char* name);

} // namespace il2cpp
