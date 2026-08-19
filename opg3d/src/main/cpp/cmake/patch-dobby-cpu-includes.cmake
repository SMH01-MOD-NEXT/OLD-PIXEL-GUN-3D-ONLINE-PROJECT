# Патч для Dobby @ 5dfc8546954ce3b3198132ab13fddb89ee92cdd7.
#
# Апстрим-баг: рефакторинг jmpews/Dobby@a588a0df ("Refactor? Yes") удалил
# заголовок source/core/arch/Cpu.h (umbrella для CpuRegister.h + CpuFeature.h),
# но оставил на него #include в нескольких файлах, из-за чего сборка падает:
#   fatal error: 'core/arch/Cpu.h' file not found
#
# FetchContent вызывает этот скрипт через PATCH_COMMAND сразу после выгрузки
# исходников Dobby. Скрипт идемпотентен: повторный запуск — no-op.
#
# DOBBY_SRC — корень исходников Dobby (передаётся как -DDOBBY_SRC=<SOURCE_DIR>).

if(NOT DEFINED DOBBY_SRC)
  message(FATAL_ERROR "DOBBY_SRC не задан")
endif()

# Этим заголовкам из старого Cpu.h нужен только RegisterBase, который после
# рефакторинга живёт в core/arch/CpuRegister.h. registers-x86.h и cpu-x86.h
# тоже чиним: assembler-ia32.cc / codegen-ia32.cc компилируются для всех целей,
# включая Android ARM.
set(_swap_to_cpu_register
  "source/core/arch/arm/registers-arm.h"
  "source/core/arch/x86/registers-x86.h"
  "source/core/arch/x86/cpu-x86.h"
)

# code-patch-tool-posix.cc не использует ничего из старого Cpu.h: ClearCache
# объявлен в PlatformUnifiedInterface/ExecMemory/ClearCacheTool.h (приходит
# через dobby/dobby_internal.h). Инклуд просто удаляем.
set(_drop_include
  "source/Backend/UserMode/ExecMemory/code-patch-tool-posix.cc"
)

# clear-cache-tool/*-dummy.cc тоже содержат старый инклуд, но осознанно не
# трогаем: они не участвуют в сборке (используется self-contained
# clear-cache-tool-all.c), а класс CpuFeatures, который они определяют,
# из этого снапшота удалён полностью.

foreach(_rel IN LISTS _swap_to_cpu_register)
  set(_path "${DOBBY_SRC}/${_rel}")
  if(EXISTS "${_path}")
    file(READ "${_path}" _content)
    string(REPLACE
      "#include \"core/arch/Cpu.h\""
      "#include \"core/arch/CpuRegister.h\""
      _content "${_content}"
    )
    file(WRITE "${_path}" "${_content}")
  endif()
endforeach()

foreach(_rel IN LISTS _drop_include)
  set(_path "${DOBBY_SRC}/${_rel}")
  if(EXISTS "${_path}")
    file(READ "${_path}" _content)
    string(REPLACE "#include \"core/arch/Cpu.h\"\n" "" _content "${_content}")
    file(WRITE "${_path}" "${_content}")
  endif()
endforeach()

message(STATUS "[opg3d] Dobby: исправлены устаревшие #include \"core/arch/Cpu.h\"")
