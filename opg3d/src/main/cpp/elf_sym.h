#pragma once

#include <cstdint>

// Поиск экспортов уже загруженной библиотеки без dlopen/dlsym.
//
// Зачем своё, а не dlsym: игра грузит libil2cpp.so через System.loadLibrary
// (RTLD_LOCAL + линкерные namespace'ы Android 7+), поэтому dlsym(RTLD_DEFAULT) её
// символов не видит, а dlopen из чужого namespace может вернуть отказ.
// Образ библиотеки уже отображён в наше адресное пространство, так что таблицу
// символов можно прочитать напрямую — это надёжнее и без внешних зависимостей.
namespace elfsym {

// Ищет загруженную библиотеку по имени файла (например "libil2cpp.so").
// При успехе кладёт в base_out адрес загрузки (load bias).
bool find_library(const char* soname, uintptr_t* base_out);

// Резолвит экспортируемый символ библиотеки. nullptr, если не найден.
void* find_symbol(const char* soname, const char* symbol);

} // namespace elfsym
