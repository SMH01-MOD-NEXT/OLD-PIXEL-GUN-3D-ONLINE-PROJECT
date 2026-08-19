cmake_minimum_required(VERSION 3.22.1)
project(opg3d C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Статическая либа shadowhook линкуется в SHARED libopg3d.so -> нужен PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Конфиг приходит из Gradle (opg3d/build.gradle -> arguments), дефолты — для
# ручного запуска cmake. AppID — секрет, в репо НЕ коммитим.
set(PHOTON_APP_ID "" CACHE STRING "AppID своего Photon Cloud приложения (только из CI secrets / -P)")
set(PHOTON_MODE "cloud" CACHE STRING "cloud | selfhosted")
set(PHOTON_SERVER_ADDRESS "" CACHE STRING "Адрес своего сервера (selfhosted или фикс. регион)")
set(PHOTON_SERVER_PORT "5055" CACHE STRING "Порт своего сервера (selfhosted)")
set(SHADOWHOOK_TAG "v2.0.1" CACHE STRING "Версия ShadowHook (git tag bytedance/android-inline-hook)")

# --- ShadowHook (bytedance): исходники + наш патч -------------------------------
# Почему НЕ prefab/AAR, как рекомендует апстрим:
#  1) В официальной либе bytesig ставит SIGSEGV-хендлер с флагом SA_EXPOSE_TAGBITS,
#     который ядро отклоняет (EINVAL) в 32-битных процессах (нет TIF_TAGGED_ADDR)
#     -> shadowhook_init падал с ошибкой 8 (Init bytesig mod SIGSEGV failed).
#     Апстрим-баг: https://github.com/bytedance/android-inline-hook/issues/78 (open).
#     Лечится только патчем исходников — см. cmake/patch-bytesig.cmake.
#  2) Их таргет — SHARED, а нам нужна одна самодостаточная libopg3d.so.
#  3) Их -Weverything -Werror ломается на свежем Clang (NDK r29+).
# SOURCE_SUBDIR указывает на каталог без CMakeLists.txt -> FetchContent только
# скачивает и патчит исходники, их таргеты в сборку не добавляются.
include(FetchContent)
FetchContent_Declare(
    android_inline_hook
    GIT_REPOSITORY https://github.com/bytedance/android-inline-hook.git
    GIT_TAG        ${SHADOWHOOK_TAG}
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  shadowhook   # populate-only
    PATCH_COMMAND  ${CMAKE_COMMAND} -E chdir shadowhook/src/main/cpp
                   ${CMAKE_COMMAND} -DSRC=common/bytesig.c
                   -P "${CMAKE_CURRENT_LIST_DIR}/cmake/patch-bytesig.cmake"
)
FetchContent_MakeAvailable(android_inline_hook)

# Собираем shadowhook статически сами (множество исходников — как в их CMakeLists)
if(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(SH_ARCH "arm")
elseif(ANDROID_ABI STREQUAL "arm64-v8a")
    set(SH_ARCH "arm64")
else()
    message(FATAL_ERROR "Неподдерживаемый ABI: ${ANDROID_ABI}")
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
target_compile_options(shadowhook_static PRIVATE -Os -w)   # чужой код: warnings глушим
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

# --- Наша библиотека -----------------------------------------------------------
add_library(opg3d SHARED
    main.cpp
    il2cpp.cpp
    photon_hook.cpp
)

target_include_directories(opg3d PRIVATE .)

target_compile_options(opg3d PRIVATE
    -O2
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
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

target_link_libraries(opg3d PRIVATE
    shadowhook_static
    log
    dl
)
