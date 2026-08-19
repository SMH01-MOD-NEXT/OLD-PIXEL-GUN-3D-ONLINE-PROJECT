#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Минимальная обёртка над экспортируемым C API IL2CPP из PG3D 12.5.0.
// Символы резолвятся напрямую из уже загруженной libil2cpp.so: Android linker
// namespace не обязан делать их видимыми обычному dlsym().
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
inline void*       (*object_get_class)(void* object) = nullptr;
inline void        (*field_get_value)(void* object, void* field, void* value) = nullptr;
inline void        (*field_set_value)(void* object, void* field, void* value) = nullptr;
inline void        (*field_static_get_value)(void* field, void* value) = nullptr;
inline void        (*field_static_set_value)(void* field, void* value) = nullptr;
inline void*       (*thread_attach)(void* domain) = nullptr;
inline void        (*thread_detach)(void* thread) = nullptr;

// Резолвит обязательный набор экспортов. Идемпотентно.
bool resolve();

// Поиск metadata-объектов без запуска managed-статических конструкторов.
void* find_image(const char* name);
void* find_class(const char* namespaze, const char* name);
void* find_method_info(const char* namespaze, const char* klass,
                       const char* method, int args_count);
void* method_pointer(void* method_info);
void* find_field(const char* namespaze, const char* klass, const char* field);

// Безопасное диагностическое преобразование managed UTF-16 строки.
// max_code_units ограничивает размер logcat-сообщений.
std::string to_utf8(void* managed_string, size_t max_code_units = 512);

} // namespace il2cpp
