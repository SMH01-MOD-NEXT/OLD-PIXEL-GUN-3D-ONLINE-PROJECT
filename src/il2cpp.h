#pragma once

#include <cstddef>

// Минимальный набор указателей на экспортируемый C-API IL2CPP.
// Все символы проверены по .dynsym libil2cpp.so из PG3D 12.5.0 (metadata v22).
//
// inline-переменные (C++17): заголовок можно включать в несколько TU.
namespace il2cpp {

inline void*       (*string_new)(const char* str) = nullptr;
inline void*       (*domain_get)() = nullptr;
inline void**      (*domain_get_assemblies)(void* domain, size_t* count) = nullptr;
inline void*       (*assembly_get_image)(void* assembly) = nullptr;
inline const char* (*image_get_name)(void* image) = nullptr;
inline void*       (*domain_assembly_open)(void* domain, const char* name) = nullptr;
inline void*       (*class_from_name)(void* image, const char* namespaze, const char* name) = nullptr;
inline void*       (*class_get_field_from_name)(void* klass, const char* name) = nullptr;
inline void        (*field_static_get_value)(void* field, void* value) = nullptr;
inline void        (*runtime_class_init)(void* klass) = nullptr;  // опционально, но в 12.5.0 есть

// Резолвит символы через dlsym(RTLD_DEFAULT). Идемпотентно (можно дёргать в цикле
// ожидания). true = весь обязательный минимум на месте.
bool resolve();

// Ищет image сборки по имени (например "Assembly-CSharp.dll"). nullptr, если не найден.
void* find_image(const char* name);

} // namespace il2cpp
