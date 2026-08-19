#include "elf_sym.h"

#include <cstring>

#include <elf.h>
#include <link.h>

namespace elfsym {
namespace {

struct Found {
    const char* soname;
    uintptr_t base;
    const ElfW(Phdr)* phdr;
    ElfW(Half) phnum;
    bool ok;
};

bool name_matches(const char* path, const char* soname) {
    if (path == nullptr || path[0] == '\0') return false;
    const char* slash = std::strrchr(path, '/');
    const char* file = (slash != nullptr) ? slash + 1 : path;
    return std::strcmp(file, soname) == 0;
}

int iterate_cb(struct dl_phdr_info* info, size_t, void* data) {
    Found* found = static_cast<Found*>(data);
    if (!name_matches(info->dlpi_name, found->soname)) return 0;
    found->base = static_cast<uintptr_t>(info->dlpi_addr);
    found->phdr = info->dlpi_phdr;
    found->phnum = info->dlpi_phnum;
    found->ok = true;
    return 1;  // останавливаем обход
}

// В .dynamic адреса хранятся как vaddr и требуют сдвига на load bias,
// но часть линкеров записывает туда уже абсолютный адрес.
uintptr_t fix_addr(uintptr_t value, uintptr_t base) {
    return (value < base) ? (base + value) : value;
}

void* lookup(const Found& lib, const char* symbol) {
    const ElfW(Dyn)* dyn = nullptr;
    for (ElfW(Half) i = 0; i < lib.phnum; ++i) {
        if (lib.phdr[i].p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const ElfW(Dyn)*>(lib.base + lib.phdr[i].p_vaddr);
            break;
        }
    }
    if (dyn == nullptr) return nullptr;

    const char* strtab = nullptr;
    const ElfW(Sym)* symtab = nullptr;
    const uint32_t* hash = nullptr;

    for (; dyn->d_tag != DT_NULL; ++dyn) {
        const uintptr_t v = static_cast<uintptr_t>(dyn->d_un.d_ptr);
        switch (dyn->d_tag) {
            case DT_STRTAB:
                strtab = reinterpret_cast<const char*>(fix_addr(v, lib.base));
                break;
            case DT_SYMTAB:
                symtab = reinterpret_cast<const ElfW(Sym)*>(fix_addr(v, lib.base));
                break;
            case DT_HASH:
                hash = reinterpret_cast<const uint32_t*>(fix_addr(v, lib.base));
                break;
            default:
                break;
        }
    }
    if (strtab == nullptr || symtab == nullptr || hash == nullptr) return nullptr;

    // В SysV-хеше nchain равно полному числу записей в .dynsym.
    // У libil2cpp.so это ~741 символ, линейный проход — копейки.
    const uint32_t nchain = hash[1];
    for (uint32_t i = 0; i < nchain; ++i) {
        const ElfW(Sym)& sym = symtab[i];
        if (sym.st_name == 0 || sym.st_value == 0) continue;
        if (sym.st_shndx == SHN_UNDEF) continue;
        if (std::strcmp(strtab + sym.st_name, symbol) == 0) {
            // Бит 0 у Thumb-функций не сбрасываем: для вызова он нужен.
            return reinterpret_cast<void*>(lib.base + sym.st_value);
        }
    }
    return nullptr;
}

bool find(const char* soname, Found* out) {
    out->soname = soname;
    out->base = 0;
    out->phdr = nullptr;
    out->phnum = 0;
    out->ok = false;
    dl_iterate_phdr(iterate_cb, out);
    return out->ok && out->phdr != nullptr;
}

} // namespace

bool find_library(const char* soname, uintptr_t* base_out) {
    Found lib;
    if (!find(soname, &lib)) return false;
    if (base_out != nullptr) *base_out = lib.base;
    return true;
}

void* find_symbol(const char* soname, const char* symbol) {
    Found lib;
    if (!find(soname, &lib)) return nullptr;
    return lookup(lib, symbol);
}

} // namespace elfsym
