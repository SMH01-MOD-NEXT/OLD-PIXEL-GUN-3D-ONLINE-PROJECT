# Патч bytesig.c (shadowhook): убираем флаг SA_EXPOSE_TAGBITS из sigaction().
#
# Зачем: на 32-битных процессах (armeabi-v7a) ядро отклоняет sigaction() с этим
# флагом (EINVAL — у процесса нет TIF_TAGGED_ADDR), из-за чего shadowhook_init()
# падал с ошибкой 8 "Init bytesig mod SIGSEGV failed". Сам флаг нужен лишь для
# MTE-отладки (экспозиция tag-битов в siginfo) — на crash-protection shadowhook
# его удаление не влияет.
# Апстрим-баг: https://github.com/bytedance/android-inline-hook/issues/78
#
# Запускается FetchContent'ом после скачивания исходников (PATCH_COMMAND),
# рабочая директория — shadowhook/src/main/cpp, путь к файлу в SRC.
if(NOT DEFINED SRC)
    message(FATAL_ERROR "patch-bytesig: не задан SRC")
endif()

file(READ "${SRC}" content)
set(needle " | SA_EXPOSE_TAGBITS")
string(FIND "${content}" "${needle}" pos)
if(pos EQUAL -1)
    message(STATUS "patch-bytesig: флаг уже убран (или строка не найдена) — пропускаю")
else()
    string(REPLACE "${needle}" "" content "${content}")
    file(WRITE "${SRC}" "${content}")
    message(STATUS "patch-bytesig: SA_EXPOSE_TAGBITS убран из ${SRC}")
endif()
