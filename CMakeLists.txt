cmake_minimum_required(VERSION 3.22.1)
project(opg3d CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ANDROID)
    message(FATAL_ERROR "Собирать только Android NDK toolchain'ом (armeabi-v7a)")
endif()

# --- Конфигурация (НЕ секреты в репо; AppID приходит из CI) -------------------
set(PHOTON_APP_ID "" CACHE STRING "AppID своего Photon Cloud приложения — ТОЛЬКО из CI secrets, не коммитить!")
set(PHOTON_MODE "cloud" CACHE STRING "cloud | selfhosted")
set(PHOTON_SERVER_ADDRESS "" CACHE STRING "Адрес своего сервера (selfhosted или фикс. регион)")
set(PHOTON_SERVER_PORT "5055" CACHE STRING "Порт своего сервера (selfhosted)")
set(SHADOWHOOK_TAG "v2.0.1" CACHE STRING "Версия ShadowHook (git tag bytedance/android-inline-hook)")

# --- ShadowHook (bytedance) ----------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    android_inline_hook
    GIT_REPOSITORY https://github.com/bytedance/android-inline-hook.git
    GIT_TAG        ${SHADOWHOOK_TAG}
    GIT_SHALLOW    TRUE
    # У репо нет корневого CMakeLists (Gradle-проект) — скрипт либы лежит глубже.
    # NB: в их CMakeLists стоит cmake_minimum_required(4.0.2) — CI это учитывает.
    SOURCE_SUBDIR  shadowhook/src/main/cpp
)
FetchContent_MakeAvailable(android_inline_hook)

if(TARGET shadowhook::shadowhook)
    set(SHADOWHOOK_LIB shadowhook::shadowhook)
elseif(TARGET shadowhook)
    set(SHADOWHOOK_LIB shadowhook)
else()
    message(FATAL_ERROR "Не найден CMake-таргет shadowhook")
endif()

# --- Наша библиотека -----------------------------------------------------------
add_library(opg3d SHARED
    src/main.cpp
    src/il2cpp.cpp
    src/photon_hook.cpp
)

# У таргета shadowhook include-директории объявлены PRIVATE — путь к shadowhook.h
# добавляем себе сами.
target_include_directories(opg3d PRIVATE
    src
    ${android_inline_hook_SOURCE_DIR}/shadowhook/src/main/cpp/include
)

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
    ${SHADOWHOOK_LIB}
    log
    dl
)
