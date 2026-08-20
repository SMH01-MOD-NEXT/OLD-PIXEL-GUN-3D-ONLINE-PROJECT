# ==============================================================================
# Patches for ShadowHook sources (bytedance/android-inline-hook, tag
# SHADOWHOOK_TAG).
#
# Run by FetchContent right after the download (PATCH_COMMAND).
# Working directory: <fetched>/shadowhook/src/main/cpp
#
#   [1/2] common/bytesig.c — drop SA_EXPOSE_TAGBITS  (shadowhook_init -> 8)
#   [2/2] shadowhook.c     — disable the linker module (shadowhook_init -> 12)
#
# Both patches are the price for the "exactly one .so in the artifact"
# requirement: we link shadowhook statically into libopg3d.so instead of using
# the official prefab/AAR.
#
# The script is idempotent: running it again on already patched sources breaks
# nothing.
# ==============================================================================

# ------------------------------------------------------------------------------
# [1/2] common/bytesig.c: remove the SA_EXPOSE_TAGBITS flag from sigaction().
#
# On 32-bit armeabi-v7a the kernel rejects sigaction() with this flag (EINVAL —
# the process has no TIF_TAGGED_ADDR), which made shadowhook_init() fail with
# error 8 "Init bytesig mod SIGSEGV failed". The flag is only needed for MTE
# debugging (exposing tag bits in siginfo) and does not affect shadowhook's
# crash protection.
# Upstream bug: https://github.com/bytedance/android-inline-hook/issues/78
# ------------------------------------------------------------------------------
set(BYTESIG_SRC "common/bytesig.c")
set(BYTESIG_NEEDLE " | SA_EXPOSE_TAGBITS")

if(NOT EXISTS "${BYTESIG_SRC}")
    message(FATAL_ERROR
        "patch-shadowhook: ${BYTESIG_SRC} not found\n"
        "Working directory: ${CMAKE_CURRENT_BINARY_DIR}\n"
        "PATCH_COMMAND was expected to run from shadowhook/src/main/cpp.")
endif()

file(READ "${BYTESIG_SRC}" bytesig_content)
string(FIND "${bytesig_content}" "${BYTESIG_NEEDLE}" bytesig_pos)
if(bytesig_pos EQUAL -1)
    message(STATUS "patch-shadowhook: [1/2] SA_EXPOSE_TAGBITS absent — skipping")
else()
    string(REPLACE "${BYTESIG_NEEDLE}" "" bytesig_content "${bytesig_content}")
    file(WRITE "${BYTESIG_SRC}" "${bytesig_content}")
    message(STATUS "patch-shadowhook: [1/2] SA_EXPOSE_TAGBITS removed from ${BYTESIG_SRC}")
endif()

# ------------------------------------------------------------------------------
# [2/2] shadowhook.c: do not initialize the linker module.
#
# Upstream:
#     if (__predict_false(0 != sh_linker_init())) GOTO_END(SHADOWHOOK_ERRNO_INIT_LINKER);
#
# Internally sh_linker_init() does:
#     hook soinfo::call_constructors() / soinfo::call_destructors()
#     dlopen("libshadowhook_nothing.so", RTLD_NOW)   <-- always NULL for us
#     if (NULL == handle) { "linker: dlopen nothing.so FAILED"; return -1; }
#
# libshadowhook_nothing.so is upstream's helper empty library: it is loaded
# only to scan field offsets (load_bias, name, phdr, phnum,
# constructors_called) on a fresh soinfo. We deliberately do not ship it
# because we build shadowhook statically into the single libopg3d.so. As a
# result sh_linker_init() reliably returned -1, shadowhook_init() reported
# errno=12 (INIT_LINKER), and EVERY hook-API call was then rejected inside
# shadowhook_check_avail() -> "installed 0 managed hooks".
#
# The linker module is only needed for:
#   * pending hooks by symbol name (shadowhook_hook_sym_name on not-yet-loaded
#     .so);
#   * dl_init/dl_fini callbacks.
# We use exclusively shadowhook_hook_func_addr on an absolute method address
# obtained from IL2CPP metadata of the already loaded libil2cpp.so
# (sh_task: is_by_target_addr = true — the pending path is never used).
#
# Safety verified against the v2.0.1 sources:
#   * sh_task_init() only calls sh_linker_register_dl_{init,fini}_callback(),
#     which just append a TAILQ entry and return 0 regardless of linker-module
#     state -> no error 39 (INIT_TASK);
#   * sh_task_do() with is_by_target_addr immediately calls sh_switch_hook()
#     and never touches linker-module state;
#   * sh_linker_get_addr_info_by_addr/by_name/by_handle work through xdl, are
#     stateless and do not depend on sh_linker_init().
#
# Side benefit: we no longer inline-patch the system linker on the device.
# Previously step 1 managed to run before step 2 failed, i.e. hooks on
# soinfo::call_constructors/destructors were installed but useless —
# soinfo_offset_scan_ok stayed false and all callbacks degenerated to no-ops.
# ------------------------------------------------------------------------------
set(SHADOWHOOK_SRC "shadowhook.c")
set(LINKER_MARK "OPG3D_LINKER_MOD_DISABLED")
set(LINKER_NEEDLE "if (__predict_false(0 != sh_linker_init())) GOTO_END(SHADOWHOOK_ERRNO_INIT_LINKER);")
set(LINKER_REPLACEMENT "(void)0; /* ${LINKER_MARK}: see cmake/patch-shadowhook.cmake */")

if(NOT EXISTS "${SHADOWHOOK_SRC}")
    message(FATAL_ERROR
        "patch-shadowhook: ${SHADOWHOOK_SRC} not found\n"
        "Working directory: ${CMAKE_CURRENT_BINARY_DIR}")
endif()

file(READ "${SHADOWHOOK_SRC}" shadowhook_content)
string(FIND "${shadowhook_content}" "${LINKER_NEEDLE}" linker_pos)
if(NOT linker_pos EQUAL -1)
    string(REPLACE "${LINKER_NEEDLE}" "${LINKER_REPLACEMENT}"
           shadowhook_content "${shadowhook_content}")
    file(WRITE "${SHADOWHOOK_SRC}" "${shadowhook_content}")
    message(STATUS "patch-shadowhook: [2/2] linker module disabled in ${SHADOWHOOK_SRC}")
else()
    string(FIND "${shadowhook_content}" "${LINKER_MARK}" linker_mark_pos)
    if(NOT linker_mark_pos EQUAL -1)
        message(STATUS "patch-shadowhook: [2/2] linker module already disabled — skipping")
    else()
        # Fail the build on purpose: without this patch libopg3d.so builds, but
        # shadowhook_init() returns 12 and NOT A SINGLE hook gets installed.
        # Shipping such an artifact silently is not acceptable.
        message(FATAL_ERROR
            "patch-shadowhook: [2/2] the line to patch was not found in ${SHADOWHOOK_SRC}:\n"
            "    ${LINKER_NEEDLE}\n"
            "Upstream has probably changed shadowhook_init(). The build is stopped on\n"
            "purpose: without this patch the library builds but silently hooks nothing\n"
            "(shadowhook_init -> errno 12, INIT_LINKER).\n"
            "Check the SHADOWHOOK_TAG value and the current sh_linker_init() code.")
    endif()
endif()

message(STATUS "patch-shadowhook: done")
