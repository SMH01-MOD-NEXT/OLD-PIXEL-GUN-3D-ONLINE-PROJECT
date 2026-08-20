cmake_minimum_required(VERSION 3.22.1)
project(opg3d C CXX ASM)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Статическая libshadowhook линкуется внутрь SHARED libopg3d.so -> нужен PIC.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Конфигурация приходит из Gradle. AppID является credential и никогда не
# хранится в репозитории.
set(PHOTON_APP_ID "" CACHE STRING "Photon Cloud AppID (CI secret / local -P)")
set(PHOTON_MODE "cloud" CACHE STRING "cloud | selfhosted")
set(PHOTON_SERVER_ADDRESS "" CACHE STRING "Optional fixed/self-hosted address")
set(PHOTON_SERVER_PORT "5055" CACHE STRING "Optional fixed/self-hosted port")
set(SHADOWHOOK_TAG "v2.0.1" CACHE STRING "ShadowHook version (git tag bytedance/android-inline-hook)")

# --- ShadowHook (bytedance): исходники + наши патчи -----------------------------
# ShadowHook — ARM inline-hook движок. Резолв символов il2cpp делает наш
# собственный elf_sym/.dynsym-резолвер, а shadowhook используется ТОЛЬКО для
# самого inline-хука по абсолютному адресу (shadowhook_hook_func_addr).
#
# Собираем shadowhook статически сами и линкуем внутрь единственного артефакта
# libopg3d.so (CI требует ровно один .so). Почему НЕ prefab/AAR, как советует
# апстрим:
#   1) prefab тянет отдельные libshadowhook.so + libshadowhook_nothing.so и
#      ломает требование «один .so в артефакте»;
#   2) на 32-битном armeabi-v7a официальный bytesig ставит SIGSEGV-обработчик с
#      флагом SA_EXPOSE_TAGBITS, который ядро отклоняет (EINVAL) — из-за этого
#      shadowhook_init() падал с ошибкой 8 (INIT_SIGSEGV);
#   3) их таргет собирается с -Weverything -Werror и ломается на свежем Clang.
#
# Плата за «один .so» — два патча апстрима, оба в cmake/patch-shadowhook.cmake:
#   * common/bytesig.c — снять SA_EXPOSE_TAGBITS  (ошибка 8,  INIT_SIGSEGV);
#   * shadowhook.c     — отключить linker-модуль  (ошибка 12, INIT_LINKER).
#
# Про ошибку 12 подробнее. sh_linker_init() хукает soinfo::call_constructors()
# и затем делает dlopen("libshadowhook_nothing.so"), чтобы на свежезагруженной
# библиотеке просканировать раскладку полей struct soinfo. Этот служебный .so мы
# намеренно не поставляем (см. п.1), поэтому dlopen всегда возвращает NULL,
# sh_linker_init() всегда -1, shadowhook_init() всегда отдаёт INIT_LINKER, и
# дальше каждый hook-вызов отбивается внутри shadowhook_check_avail().
# Linker-модуль нужен ровно для двух вещей: pending-хуки по имени символа
# (shadowhook_hook_sym_name для ещё не загруженных .so) и dl_init/dl_fini-
# колбэки. Мы не используем ни то, ни другое — только shadowhook_hook_func_addr
# по готовому адресу внутри уже загруженной libil2cpp.so, — поэтому просто не
# инициализируем этот модуль.
#
# SOURCE_SUBDIR указывает на каталог без CMakeLists.txt -> FetchContent только
# скачивает и патчит исходники, не подключая таргеты апстрима в нашу сборку.
include(FetchContent)
FetchContent_Declare(
    android_inline_hook
    GIT_REPOSITORY https://github.com/bytedance/android-inline-hook.git
    GIT_TAG        ${SHADOWHOOK_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    SOURCE_SUBDIR  shadowhook   # populate-only, таргеты апстрима не подключаем
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
# Чужой код: глушим предупреждения и оптимизируем по размеру.
target_compile_options(shadowhook_static PRIVATE -Os -w)
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
    elf_sym.cpp
    il2cpp.cpp
    hook.cpp
    photon_hooks.cpp
)

target_include_directories(opg3d PRIVATE .)

# -fno-exceptions здесь не про размер бинарника, а про совместимость
# разматывателей стека. Не убирать.
#
# В одном процессе живут ДВА разных ARM EHABI unwinder'а:
#   * libil2cpp.so несёт GNU-совместимый (___Unwind_RaiseException ->
#     __gnu_Unwind_RaiseException). Управляемые исключения IL2CPP реализует
#     через C++ исключения, поэтому любой NullReference/SocketException внутри
#     игрового метода поднимает именно его;
#   * мы статически линкуем LLVM libc++abi + libunwind, иначе не выйдет
#     «один .so» в артефакте.
#
# Их _Unwind_Context* несовместимы между собой. Пока в наших функциях есть
# landing pad'ы (а они появляются от любого объекта с деструктором — например
# std::string в хуках), Clang выдаёт для кадра exidx-запись в generic model со
# ссылкой PREL31 на НАШ __gxx_personality_v0. Разматыватель libil2cpp доходит
# до кадра хука, читает эту ссылку и зовёт нашу personality со СВОИМ
# контекстом; LLVM-овый _Unwind_VRS_Set принимает его за unw_cursor_t ->
# SIGSEGV в __unw_set_reg (в логе: fault addr 0x7).
#
# Ни try/catch в хуке, ни проверки указателей это не лечат: падение происходит
# в фазе поиска обработчика, ДО того как какой-либо catch мог бы отработать.
#
# С -fno-exceptions landing pad'ов и LSDA нет, поэтому наши кадры описываются
# compact model, а её personality (__aeabi_unwind_cpp_pr0/1/2) разматыватель
# libil2cpp резолвит внутри себя и вызывает со своим же контекстом —
# исключение проходит сквозь кадр хука корректно, как сквозь обычный C-код.
#
# -funwind-tables обязателен явно: без таблиц кадры получили бы
# EXIDX_CANTUNWIND, разматывание упёрлось бы в наш кадр и упало уже в
# std::terminate. Регрессию стережёт шаг "Assert no LLVM C++ personality"
# в .github/workflows/build.yml.
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

target_link_libraries(opg3d PRIVATE
    shadowhook_static
    log
    dl
)
