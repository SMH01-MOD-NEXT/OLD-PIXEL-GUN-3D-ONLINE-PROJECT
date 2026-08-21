#pragma once

#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

#include "cloud_guard.h"
#include "hook.h"
#include "log.h"

// Version-specific compatibility points for the supplied PG3D 14.1.1 ARMv7
// libil2cpp.so (SHA-256
// 5e4ab38ad6388ab187d0e7441ea6833838414fee5720d9dd483bf1e10eb33219).
// All ordinary features remain metadata-resolved. The only raw instruction
// change is the startup APK-signature decision, because that comparison lives
// inside a compiler-generated coroutine and has no safe managed boundary.
namespace version_1411 {
namespace detail {

using MethodInfo = void;
using ConnectSquadFn = bool (*)(bool siege_squad, const MethodInfo* method);

inline ConnectSquadFn g_connect_squad = nullptr;

// AppsMenu.<Start>c__Iterator1.MoveNext(), 14.1.1:
//   0xA9A980  cmp r0, #0
//   0xA9A984  beq 0xA9AAF0  ; signature accepted
// Re-signing takes the fall-through path, persists a tamper marker and queues
// ClosingScene. Only the condition code is changed (EQ -> AL); package-name,
// null/error and every unrelated startup check remain stock.
inline constexpr uintptr_t kSignatureDecisionRva = 0x00A9A984u;
inline constexpr uint32_t kExpectedCmp = 0xE3500000u;
inline constexpr uint32_t kExpectedBranch = 0x0A000059u;
inline constexpr uint32_t kAcceptedBranch = 0xEA000059u;

bool patch_word(uintptr_t address, uint32_t replacement) {
    const long page_size_raw = sysconf(_SC_PAGESIZE);
    if (page_size_raw <= 0) {
        LOGE("14.1.1: sysconf(_SC_PAGESIZE) failed");
        return false;
    }
    const uintptr_t page_size = static_cast<uintptr_t>(page_size_raw);
    const uintptr_t page = address & ~(page_size - 1u);
    if (mprotect(reinterpret_cast<void*>(page), page_size,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("14.1.1: mprotect RWX failed for signature decision: %s",
             std::strerror(errno));
        return false;
    }

    auto* word = reinterpret_cast<volatile uint32_t*>(address);
    *word = replacement;
    __builtin___clear_cache(reinterpret_cast<char*>(address),
                            reinterpret_cast<char*>(address + sizeof(uint32_t)));

    const bool written = (*word == replacement);
    if (mprotect(reinterpret_cast<void*>(page), page_size,
                 PROT_READ | PROT_EXEC) != 0) {
        LOGW("14.1.1: could not restore RX protection after signature patch: %s",
             std::strerror(errno));
    }
    return written;
}

void* replacement(ConnectSquadFn fn) {
    return reinterpret_cast<void*>(fn);
}

void** original_slot(ConnectSquadFn* fn) {
    return reinterpret_cast<void**>(fn);
}

bool hook_connect_squad(bool siege_squad, const MethodInfo* method) {
    cloud_guard::detail::force_cloud(
        "GameConnect.ConnectToPhotonSquad(bool)", true);
    return g_connect_squad != nullptr
               ? g_connect_squad(siege_squad, method)
               : false;
}

} // namespace detail

inline bool install_early_signature_patch(uintptr_t il2cpp_base) {
    if (il2cpp_base == 0u) {
        LOGE("14.1.1: no libil2cpp base for signature compatibility patch");
        return false;
    }

    const uintptr_t decision = il2cpp_base + detail::kSignatureDecisionRva;
    const uint32_t previous =
        *reinterpret_cast<const volatile uint32_t*>(decision - 4u);
    const uint32_t current =
        *reinterpret_cast<const volatile uint32_t*>(decision);

    if (previous != detail::kExpectedCmp) {
        LOGE("14.1.1: signature patch refused: preceding opcode at "
             "RVA 0x%08" PRIxPTR " is 0x%08" PRIx32 " (expected 0x%08" PRIx32 ")",
             detail::kSignatureDecisionRva - 4u, previous,
             detail::kExpectedCmp);
        return false;
    }
    if (current == detail::kAcceptedBranch) {
        LOGI("14.1.1: APK signature decision already accepts the current "
             "certificate");
        return true;
    }
    if (current != detail::kExpectedBranch) {
        LOGE("14.1.1: signature patch refused: opcode at RVA 0x%08" PRIxPTR
             " is 0x%08" PRIx32 " (expected 0x%08" PRIx32 ")",
             detail::kSignatureDecisionRva, current,
             detail::kExpectedBranch);
        return false;
    }

    if (!detail::patch_word(decision, detail::kAcceptedBranch)) {
        LOGE("14.1.1: APK signature acceptance branch was not written");
        return false;
    }

    LOGI("14.1.1: APK re-sign compatibility active at RVA 0x%08" PRIxPTR
         " (only signature mismatch -> success; package/error checks stock)",
         detail::kSignatureDecisionRva);
    return true;
}

inline bool install_runtime_hooks() {
    // 13.2.1 exposed ConnectToPhotonSquad() with no managed arguments.
    // 14.1.1 added the siegeSquad bool, so the old optional hook no longer
    // resolves. Cover the new overload explicitly and preserve its argument.
    const bool squad = hook::install(
        {"", "GameConnect", "ConnectToPhotonSquad", 1},
        detail::replacement(&detail::hook_connect_squad),
        detail::original_slot(&detail::g_connect_squad), true);
    if (!squad) {
        LOGE("14.1.1: GameConnect.ConnectToPhotonSquad(bool) hook failed");
        return false;
    }
    LOGI("14.1.1: squad Photon cloud-route hook armed (siege flag preserved)");
    return true;
}

} // namespace version_1411
