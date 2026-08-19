#pragma once

#include <cstddef>

// Минимальный набор указателей на экспортируемый C-API IL2CPP.
// Все эти символы проверены в .dynsym libil2cpp.so игры (PG3D 12.5.0).
namespace il2cpp {

inline void*       (*string_new)(const char* str) = nullptr;
inline void*       (*domain_get)() = nullptr;
inline void**      (*domain_get_assemblies)(void* domain, size_t* count) = nullptr;
inline void*       (*assembly_get_image)(void* assembly) = nullptr;
inline const char* (*image_get_name)(void* image) = nullptr;
inline void*       (*class_from_name)(void* image, const char* namespaze, const char* name) = nullptr;
inline void*       (*class_get_field_from_name)(void* klass, const char* name) = nullptr;
inline void        (*field_static_get_value)(void* field, void* value) = nullptr;
inline void*       (*thread_attach)(void* domain) = nullptr;
inline void        (*thread_detach)(void* thread) = nullptr;

// ВНИМАНИЕ: il2cpp_runtime_class_init здесь сознательно НЕ используется.
// Он запускает статический конструктор класса, а у PhotonNetwork в нём идёт
// Resources.Load — вызов из фонового потока Unity запрещает (и типизированное
// исключение сломало бы Photon навсегда). Ждём, пока игра сама всё инициализирует.

// Резолвит символы из libil2cpp.so через чтение .dynsym (без dlsym).
// Тихо, идемпотентно. true = весь обязательный минимум на месте.
bool resolve();

// Ищет image сборки по имени (например "Assembly-CSharp.dll"). nullptr, если нет.
void* find_image(const char* name);

} // namespace il2cpp
