cmake_minimum_required(VERSION 3.22.1)
project(opg3d C CXX ASM)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static libshadowhook is linked into the SHARED libopg3d.so -> PIC is required.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Configuration comes from Gradle. The AppID is a credential and is never
# stored in the repository.
set(PHOTON_APP_ID "" CACHE STRING "Photon Cloud AppID (CI secret / local -P)")
set(PHOTON_MODE "cloud" CACHE STRING "cloud | selfhosted")
set(PHOTON_SERVER_ADDRESS "" CACHE STRING "Optional fixed/self-hosted address")
set(PHOTON_SERVER_PORT "5055" CACHE STRING "Optional fixed/self-hosted port")
set(SHADOWHOOK_TAG "v2.0.1" CACHE STRING "ShadowHook version (git tag bytedance/android-inline-hook)")

# --- ShadowHook (bytedance): sources + our patches ----------------------------
# ShadowHook is the ARM inline-hook engine. IL2CPP symbol resolution is done
# by our own elf_sym/.dynsym resolver; shadowhook is used ONLY for the inline
# hook itself at an absolute address (shadowhook_hook_func_addr).
#
# We build shadowhook statically ourselves and link it into one self-contained
# runtime library. The release export also keeps an unstripped symbol copy of
# that same libopg3d.so. Why NOT the upstream prefab/AAR:
#   1) prefab pulls in separate libshadowhook.so + libshadowhook_nothing.so
#      and breaks the "one self-contained runtime library" requirement;
#   2) on 32-bit armeabi-v7a the official bytesig installs a SIGSEGV handler
#      with the SA_EXPOSE_TAGBITS flag, which the kernel rejects (EINVAL) —
#      that's why shadowhook_init() used to fail with error 8 (INIT_SIGSEGV);
#   3) their target builds with -Weverything -Werror and breaks on newer Clang.
#
# The price for "one .so" is two upstream patches, both in
# cmake/patch-shadowhook.cmake:
#   * common/bytesig.c — drop SA_EXPOSE_TAGBITS  (error 8,  INIT_SIGSEGV);
#   * shadowhook.c     — disable the linker module (error 12, INIT_LINKER).
#
# More on error 12. sh_linker_init() hooks soinfo::call_constructors() and
# then dlopen()s "libshadowhook_nothing.so" to scan the field layout of struct
# soinfo on a freshly loaded library. We deliberately do not ship that helper
# .so (see 1), so dlopen always returns NULL, sh_linker_init() always returns
# -1, shadowhook_init() always reports INIT_LINKER, and every hook call is then
# rejected inside shadowhook_check_avail().
# The linker module exists for exactly two things: pending hooks by symbol name
# (shadowhook_hook_sym_name on not-yet-loaded .so) and dl_init/dl_fini
# callbacks. We use neither — only shadowhook_hook_func_addr on a ready address
# inside the already loaded libil2cpp.so — so we simply never init that module.
#
# SOURCE_SUBDIR points at a directory without CMakeLists.txt -> FetchContent
# only downloads and patches the sources without adding upstream targets to
# our build.
include(FetchContent)
FetchContent_Declare(
    android_inline_hook
    GIT_REPOSITORY https://github.com/bytedance/android-inline-hook.git
    GIT_TAG        ${SHADOWHOOK_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    SOURCE_SUBDIR  shadowhook   # populate-only, upstream targets stay out
    PATCH_COMMAND  ${CMAKE_COMMAND} -E chdir shadowhook/src/main/cpp
                   ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_LIST_DIR}/cmake/patch-shadowhook.cmake"
)
FetchContent_MakeAvailable(android_inline_hook)

if(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(SH_ARCH "arm")
elseif(ANDROID_ABI STREQUAL "arm64-v8a")
    set(SH_ARCH "arm64")
else()
    message(FATAL_ERROR "Unsupported ABI for shadowhook: ${ANDROID_ABI}")
endif()

set(SH_SRC_DIR ${android_inline_hook_SOURCE_DIR}/shadowhook/src/main/cpp)
file(GLOB SH_SRC
    ${SH_SRC_DIR}/*.c
    ${SH_SRC_DIR}/arch/${SH_ARCH}/*.c
    ${SH_SRC_DIR}/arch/${SH_ARCH}/*.S
    ${SH_SRC_DIR}/common/*.c
    ${SH_SRC_DIR}/third_party/*/*.c
)
add_library(shadowhook_static STATIC ${SH_SRC})
target_compile_features(shadowhook_static PRIVATE c_std_11)
# Third-party code: silence warnings and optimize for size.
target_compile_options(shadowhook_static PRIVATE -Os -w)
# ShadowHook normally overwrites one A32 instruction and jumps through an
# 8-byte island allocated within the target's +/-32 MiB branch range. The
# 14.1.1 libil2cpp mapping has no usable island near three PhotonNetwork
# methods, so the first strategy returns errno 40. Enable ShadowHook's own
# second strategy for this verified ARM32 target: after an island allocation
# failure it rewrites the first two regular A32 prologue instructions with an
# 8-byte absolute jump. The island strategy remains the first choice for all
# other hooks.
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    target_compile_definitions(shadowhook_static PRIVATE
        SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND=1
    )
endif()
target_include_directories(shadowhook_static
    PUBLIC  ${SH_SRC_DIR}/include
    PRIVATE ${SH_SRC_DIR}
            ${SH_SRC_DIR}/arch/${SH_ARCH}
            ${SH_SRC_DIR}/common
            ${SH_SRC_DIR}/third_party/xdl
            ${SH_SRC_DIR}/third_party/bsd
            ${SH_SRC_DIR}/third_party/lss
)
target_link_libraries(shadowhook_static PUBLIC log dl)

# --- Our library --------------------------------------------------------------
add_library(opg3d SHARED
    main.cpp
    elf_sym.cpp
    il2cpp.cpp
    hook.cpp
    photon_hooks.cpp
)

target_include_directories(opg3d PRIVATE .)

# -fno-exceptions is not about binary size but about stack-unwinder
# compatibility. Do not remove it.
#
# ONE process hosts TWO different ARM EHABI unwinders:
#   * libil2cpp.so carries the GNU-compatible one (___Unwind_RaiseException ->
#     __gnu_Unwind_RaiseException). IL2CPP implements managed exceptions via
#     C++ exceptions, so any NullReference/SocketException inside a game method
#     is raised through exactly that unwinder;
#   * we statically link LLVM libc++abi + libunwind, otherwise the "one .so"
#     artifact would not be possible.
#
# Their _Unwind_Context* are mutually incompatible. While our functions have
# landing pads (and any object with a destructor creates one — e.g. std::string
# in the hooks), Clang emits an exidx entry for the frame in generic model with
# a PREL31 reference to OUR __gxx_personality_v0. The libil2cpp unwinder
# reaches the hook frame, reads that reference and calls our personality with
# ITS context; LLVM's _Unwind_VRS_Set mistakes it for an unw_cursor_t ->
# SIGSEGV in __unw_set_reg (in the log: fault addr 0x7).
#
# Neither try/catch in the hook nor pointer checks fix this: the crash happens
# during the handler-search phase, BEFORE any catch could run.
#
# With -fno-exceptions there are no landing pads and no LSDA, so our frames are
# described by the compact model, whose personality (__aeabi_unwind_cpp_pr0/1/2)
# the libil2cpp unwinder resolves inside itself and calls with its own
# context — the exception passes through the hook frame correctly, like through
# plain C code.
#
# -funwind-tables is required explicitly: without tables the frames would get
# EXIDX_CANTUNWIND, unwinding would stop at our frame and fail in
# std::terminate. The regression is guarded by the "Assert no LLVM C++
# personality" step in .github/workflows/build.yml.
target_compile_options(opg3d PRIVATE
    -O2
    -fno-exceptions
    -funwind-tables
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)
target_link_options(opg3d PRIVATE
    -Wl,--gc-sections
    -Wl,--exclude-libs,ALL
)

target_compile_definitions(opg3d PRIVATE
    PHOTON_APP_ID="${PHOTON_APP_ID}"
    PHOTON_SERVER_ADDRESS="${PHOTON_SERVER_ADDRESS}"
    PHOTON_SERVER_PORT=${PHOTON_SERVER_PORT}
)
if(PHOTON_MODE STREQUAL "selfhosted")
    target_compile_definitions(opg3d PRIVATE PHOTON_MODE_SELFHOSTED=1)
endif()

# z: obb_provisioner.h reads the APK as a plain ZIP with syscalls, and both
# things it needs from zlib are unavoidable there — AndroidManifest.xml is
# always deflated inside an APK (that is where the package name and the version
# code come from), and the extracted expansion file is verified against the
# CRC-32 recorded in the archive. libz is an NDK-public system library, so this
# adds a DT_NEEDED on libz.so and nothing to the artifact.
target_link_libraries(opg3d PRIVATE
    shadowhook_static
    log
    dl
    z
)
