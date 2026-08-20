# ==============================================================================
# Патчи исходников ShadowHook (bytedance/android-inline-hook, тег SHADOWHOOK_TAG).
#
# Запускается FetchContent'ом сразу после скачивания (PATCH_COMMAND).
# Рабочая директория: <fetched>/shadowhook/src/main/cpp
#
#   [1/2] common/bytesig.c — снять SA_EXPOSE_TAGBITS  (shadowhook_init -> 8)
#   [2/2] shadowhook.c     — отключить linker-модуль  (shadowhook_init -> 12)
#
# Оба патча — плата за требование «ровно один .so в артефакте»: мы линкуем
# shadowhook статически внутрь libopg3d.so вместо официального prefab/AAR.
#
# Скрипт идемпотентен: повторный запуск на уже пропатченных исходниках ничего
# не ломает и ничего не портит.
# ==============================================================================

# ------------------------------------------------------------------------------
# [1/2] common/bytesig.c: убрать флаг SA_EXPOSE_TAGBITS из sigaction().
#
# На 32-битном armeabi-v7a ядро отклоняет sigaction() с этим флагом (EINVAL —
# у процесса нет TIF_TAGGED_ADDR), из-за чего shadowhook_init() падал с ошибкой
# 8 "Init bytesig mod SIGSEGV failed". Флаг нужен только для MTE-отладки
# (экспозиция tag-битов в siginfo) и на crash-protection shadowhook не влияет.
# Апстрим-баг: https://github.com/bytedance/android-inline-hook/issues/78
# ------------------------------------------------------------------------------
set(BYTESIG_SRC "common/bytesig.c")
set(BYTESIG_NEEDLE " | SA_EXPOSE_TAGBITS")

if(NOT EXISTS "${BYTESIG_SRC}")
    message(FATAL_ERROR
        "patch-shadowhook: не найден ${BYTESIG_SRC}\n"
        "Рабочая директория: ${CMAKE_CURRENT_BINARY_DIR}\n"
        "Ожидалось, что PATCH_COMMAND запущен из shadowhook/src/main/cpp.")
endif()

file(READ "${BYTESIG_SRC}" bytesig_content)
string(FIND "${bytesig_content}" "${BYTESIG_NEEDLE}" bytesig_pos)
if(bytesig_pos EQUAL -1)
    message(STATUS "patch-shadowhook: [1/2] SA_EXPOSE_TAGBITS отсутствует — пропускаю")
else()
    string(REPLACE "${BYTESIG_NEEDLE}" "" bytesig_content "${bytesig_content}")
    file(WRITE "${BYTESIG_SRC}" "${bytesig_content}")
    message(STATUS "patch-shadowhook: [1/2] SA_EXPOSE_TAGBITS убран из ${BYTESIG_SRC}")
endif()

# ------------------------------------------------------------------------------
# [2/2] shadowhook.c: не инициализировать linker-модуль.
#
# Апстрим:
#     if (__predict_false(0 != sh_linker_init())) GOTO_END(SHADOWHOOK_ERRNO_INIT_LINKER);
#
# sh_linker_init() внутри делает:
#     hook soinfo::call_constructors() / soinfo::call_destructors()
#     dlopen("libshadowhook_nothing.so", RTLD_NOW)   <-- всегда NULL у нас
#     if (NULL == handle) { "linker: dlopen nothing.so FAILED"; return -1; }
#
# libshadowhook_nothing.so — служебная пустая библиотека апстрима: её грузят
# только чтобы на свежем soinfo просканировать смещения полей (load_bias, name,
# phdr, phnum, constructors_called). Мы её намеренно не поставляем, потому что
# собираем shadowhook статически в единственный libopg3d.so. Из-за этого
# sh_linker_init() возвращал -1 гарантированно, shadowhook_init() отдавал
# errno=12 (INIT_LINKER), и дальше КАЖДЫЙ вызов hook-API отбивался внутри
# shadowhook_check_avail() -> "installed 0 managed hooks".
#
# Linker-модуль нужен только для:
#   * pending-хуков по имени символа (shadowhook_hook_sym_name на ещё не
#     загруженные .so);
#   * dl_init/dl_fini-колбэков.
# Мы используем исключительно shadowhook_hook_func_addr по абсолютному адресу
# метода, полученному из IL2CPP-метаданных уже загруженной libil2cpp.so
# (sh_task: is_by_target_addr = true — ветка pending не задействуется вообще).
#
# Безопасность проверена по исходникам v2.0.1:
#   * sh_task_init() зовёт только sh_linker_register_dl_{init,fini}_callback(),
#     которые просто добавляют запись в TAILQ и возвращают 0 вне зависимости от
#     инициализации linker-модуля -> ошибки 39 (INIT_TASK) не будет;
#   * sh_task_do() при is_by_target_addr сразу вызывает sh_switch_hook() и к
#     состоянию linker-модуля не обращается;
#   * sh_linker_get_addr_info_by_addr/by_name/by_handle работают через xdl,
#     они stateless и от sh_linker_init() не зависят.
#
# Побочный плюс: перестаём инлайн-патчить системный linker на устройстве.
# Раньше шаг 1 успевал отработать до падения на шаге 2, то есть хуки на
# soinfo::call_constructors/destructors ставились, но были бесполезны —
# soinfo_offset_scan_ok оставался false и все колбэки вырождались в no-op.
# ------------------------------------------------------------------------------
set(SHADOWHOOK_SRC "shadowhook.c")
set(LINKER_MARK "OPG3D_LINKER_MOD_DISABLED")
set(LINKER_NEEDLE "if (__predict_false(0 != sh_linker_init())) GOTO_END(SHADOWHOOK_ERRNO_INIT_LINKER);")
set(LINKER_REPLACEMENT "(void)0; /* ${LINKER_MARK}: см. cmake/patch-shadowhook.cmake */")

if(NOT EXISTS "${SHADOWHOOK_SRC}")
    message(FATAL_ERROR
        "patch-shadowhook: не найден ${SHADOWHOOK_SRC}\n"
        "Рабочая директория: ${CMAKE_CURRENT_BINARY_DIR}")
endif()

file(READ "${SHADOWHOOK_SRC}" shadowhook_content)
string(FIND "${shadowhook_content}" "${LINKER_NEEDLE}" linker_pos)
if(NOT linker_pos EQUAL -1)
    string(REPLACE "${LINKER_NEEDLE}" "${LINKER_REPLACEMENT}"
           shadowhook_content "${shadowhook_content}")
    file(WRITE "${SHADOWHOOK_SRC}" "${shadowhook_content}")
    message(STATUS "patch-shadowhook: [2/2] linker-модуль отключён в ${SHADOWHOOK_SRC}")
else()
    string(FIND "${shadowhook_content}" "${LINKER_MARK}" linker_mark_pos)
    if(NOT linker_mark_pos EQUAL -1)
        message(STATUS "patch-shadowhook: [2/2] linker-модуль уже отключён — пропускаю")
    else()
        # Намеренно валим сборку: без этого патча libopg3d.so соберётся, но
        # shadowhook_init() вернёт 12 и НИ ОДИН хук не встанет. Молчаливо
        # выпускать такой артефакт нельзя.
        message(FATAL_ERROR
            "patch-shadowhook: [2/2] в ${SHADOWHOOK_SRC} не найдена патчимая строка:\n"
            "    ${LINKER_NEEDLE}\n"
            "Похоже, апстрим изменил shadowhook_init(). Сборка остановлена намеренно:\n"
            "без этого патча библиотека собирается, но молча не хукает ничего\n"
            "(shadowhook_init -> errno 12, INIT_LINKER).\n"
            "Проверь тег SHADOWHOOK_TAG и актуальный код sh_linker_init().")
    endif()
endif()

message(STATUS "patch-shadowhook: готово")
