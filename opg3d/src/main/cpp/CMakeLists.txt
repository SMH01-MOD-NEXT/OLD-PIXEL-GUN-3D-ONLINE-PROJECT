cmake_minimum_required(VERSION 3.22.1)
project(opg3d CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Конфиг приходит из Gradle (opg3d/build.gradle -> arguments), дефолты — для
# ручного запуска cmake. AppID — секрет, в репо НЕ коммитим.
set(PHOTON_APP_ID "" CACHE STRING "AppID своего Photon Cloud приложения (только из CI secrets / -P)")
set(PHOTON_MODE "cloud" CACHE STRING "cloud | selfhosted")
set(PHOTON_SERVER_ADDRESS "" CACHE STRING "Адрес своего сервера (selfhosted или фикс. регион)")
set(PHOTON_SERVER_PORT "5055" CACHE STRING "Порт своего сервера (selfhosted)")

# --- Никаких внешних зависимостей -------------------------------------------
# Раньше тут через FetchContent тянулся ShadowHook. Он выброшен полностью:
#  1) инлайн-хук нам не нужен — мы не меняем код игры, только данные (поля
#     объекта настроек Photon);
#  2) его shadowhook_init падал на реальном устройстве ещё до любого хука:
#     сначала ошибка 8 (bytesig / SA_EXPOSE_TAGBITS, upstream issue #78),
#     потом ошибка 12 (linker mod: не нашёл внутренние символы динамического
#     линкера, upstream issues #113 / #91);
#  3) без FetchContent сборка полностью оффлайновая и воспроизводимая.
# Адреса экспортов il2cpp_* берём сами — чтением .dynsym из памяти (elf_sym.cpp).

# --- Наша библиотека -----------------------------------------------------------
add_library(opg3d SHARED
    main.cpp
    elf_sym.cpp
    il2cpp.cpp
    photon_patch.cpp
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

target_link_libraries(opg3d PRIVATE
    log
    dl
)
