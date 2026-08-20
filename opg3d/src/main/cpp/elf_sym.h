#pragma once

#include <cstdint>

// Finds exports of an already loaded library without dlopen/dlsym.
//
// Why not dlsym: the game loads libil2cpp.so via System.loadLibrary
// (RTLD_LOCAL + Android 7+ linker namespaces), so dlsym(RTLD_DEFAULT) cannot
// see its symbols, and dlopen from a foreign namespace may be refused. The
// library image is already mapped into our address space, so its symbol table
// can simply be read directly — more reliable and with no external
dependencies.
namespace elfsym {

// Finds a loaded library by file name (e.g. "libil2cpp.so").
// On success, writes the load address (load bias) to base_out.
bool find_library(const char* soname, uintptr_t* base_out);

// Resolves an exported symbol of the library. nullptr if not found.
void* find_symbol(const char* soname, const char* symbol);

} // namespace elfsym
