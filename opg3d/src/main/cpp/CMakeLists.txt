cmake_minimum_required(VERSION 3.22.1)
project(opg3d C CXX ASM)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Конфигурация приходит из Gradle. AppID является credential и никогда не
# хранится в репозитории.
set(PHOTON_APP_ID "" CACHE STRING "Photon Cloud AppID (CI secret / local -P)")
set(PHOTON_MODE "cloud" CACHE STRING "cloud | selfhosted")
set(PHOTON_SERVER_ADDRESS "" CACHE STRING "Optional fixed/self-hosted address")
set(PHOTON_SERVER_PORT "5055" CACHE STRING "Optional fixed/self-hosted port")

# Dobby используется только как ARM32 inline-hook engine. Версия зафиксирована
# полным commit SHA; плагины, тесты и shared target не являются зависимостями
# libopg3d.so. dobby_static линкуется внутрь единственного артефакта.
include(FetchContent)
set(DOBBY_DEBUG OFF CACHE BOOL "" FORCE)
set(NearBranch ON CACHE BOOL "" FORCE)
set(Plugin.SymbolResolver OFF CACHE BOOL "" FORCE)
set(Plugin.ImportTableReplace OFF CACHE BOOL "" FORCE)
set(Plugin.Android.BionicLinkerUtil OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(DOBBY_BUILD_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    dobby
    GIT_REPOSITORY https://github.com/jmpews/Dobby.git
    GIT_TAG 5dfc8546954ce3b3198132ab13fddb89ee92cdd7
    GIT_SHALLOW FALSE
    GIT_PROGRESS TRUE
    # На зафиксированном коммите апстрим сломан: рефакторинг a588a0df удалил
    # source/core/arch/Cpu.h, но оставил на него #include в 6 файлах
    # ('core/arch/Cpu.h' file not found). Чиним исходники сразу после выгрузки;
    # <SOURCE_DIR> разворачивается ExternalProject в каталог dobby-src.
    PATCH_COMMAND ${CMAKE_COMMAND} -DDOBBY_SRC=<SOURCE_DIR> -P ${CMAKE_CURRENT_LIST_DIR}/cmake/patch-dobby-cpu-includes.cmake
)
FetchContent_MakeAvailable(dobby)

add_library(opg3d SHARED
    main.cpp
    elf_sym.cpp
    il2cpp.cpp
    hook.cpp
    photon_hooks.cpp
)

target_include_directories(opg3d PRIVATE .)
target_compile_options(opg3d PRIVATE
    -O2
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

# Не позволяем предупреждениям стороннего проекта ломать нашу сборку на новых
# версиях Clang; к собственному коду -Wall/-Wextra продолжают применяться.
target_compile_options(dobby_static PRIVATE -Os -w)

target_link_libraries(opg3d PRIVATE
    dobby_static
    log
    dl
)
