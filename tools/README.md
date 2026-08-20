# Расширенная диагностика OPG3D

Нативный логгер добавляет к каждой строке:

```text
[#000042 +015237ms tid=19925 pc=libil2cpp.so+0x118f750]
```

- `#` — порядок событий внутри процесса;
- `+...ms` — монотонное время с первой строки OPG3D;
- `tid` — Linux thread ID;
- `pc` — модуль и RVA непосредственного вызывающего кода.

Для хука `PhotonNetwork.Disconnect` значение `pc` показывает, **какой managed-метод вызвал Disconnect**. Используется только return address текущего кадра: полного unwinding нет, потому что смешивание ARM EHABI unwinder из `libil2cpp.so` и LLVM unwinder библиотеки уже приводило к падениям.

## Снять и расшифровать лог

Сохрани OPG3D-лог и передай соответствующий этой версии игры `dump.cs`:

```bash
adb logcat -d -v threadtime -s OPG3D > opg3d.log
python3 tools/symbolize_log.py --dump /path/to/dump.cs --log opg3d.log \
  --output opg3d.symbolized.log
```

Пример результата:

```text
... pc=libil2cpp.so+0x118f750] net: PhotonNetwork.Disconnect requested ... [managed=GameConnect.Disconnect+0x28]
```

Если несколько IL2CPP-методов используют один и тот же сгенерированный адрес, инструмент показывает до трёх имён. `PHOTON_APP_ID`, auth token и UserId по-прежнему не выводятся.

## Быстрая проверка инструмента

```bash
python3 -m unittest tools/test_symbolize_log.py
```
