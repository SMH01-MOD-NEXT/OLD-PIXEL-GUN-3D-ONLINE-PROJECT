# OLD-PIXEL-GUN-3D-ONLINE-PROJECT
Experiment with AI: creating the private server for pg3d old version (12.5.0) arm-v7

# NOTE: We DON'T CREATE A CHEAT OR SOMETHING ILLEGAL
We just need to make library that can be loaded to the game, functional: ONLY redirecting online battes to MINE photon server, NOT OFFICIAL. Official servers already dead because of old version, so we CAN'T make harm things for other players and NOT PLANNING to do this.

# We need to make online working and make Pixel Gun Great Again. Happy coding!

---

## Как это работает

Нативная библиотека `libopg3d.so` (C++, armeabi-v7a) загружается в процесс игры и через
[ShadowHook](https://github.com/bytedance/android-inline-hook) (bytedance, v2.0.1) перехватывает
`PhotonNetwork.ConnectUsingSettings(string)` — единую точку, через которую PUN classic (~1.79)
подключается к сети. Перед вызовом оригинала библиотека переписывает поля статического объекта
`PhotonNetwork.PhotonServerSettings`:

| Поле | Смещение | Что делаем |
|---|---|---|
| `HostType` | `0x0C` | `PhotonCloud` (1) или `SelfHosted` (2) — по режиму сборки |
| `ServerAddress` / `ServerPort` | `0x14` / `0x18` | свой сервер (selfhosted / фикс. регион) |
| `AppID` / `VoiceAppID` | `0x1C` / `0x20` | AppID **своего** Photon Cloud приложения |

Адреса и смещения восстановлены из `dump.cs` (IL2CPP metadata v22) и побайтово сверены с
`libil2cpp.so`: `ConnectUsingSettings` @ RVA `0xC8002C`, код скомпилирован в ARM-режиме
(thumb-бит при хуке не нужен). Весь используемый il2cpp C-API экспортирован игрой.

## AppID — НЕ хардкодим

AppID — это креды, в репозитории его нет и не будет. Он подставляется только на сборке:

1. GitHub → **Settings → Secrets and variables → Actions → New repository secret**:
   имя `PHOTON_APP_ID`, значение — AppID из Photon Dashboard.
2. Workflow `build.yml` передаёт его в CMake как `-DPHOTON_APP_ID=...`.

Сборка без секрета тоже работает: библиотека компилируется и хук встаёт, но AppID не меняется
(passthrough + warning в logcat). Удобно для форков и отладки хука.

## Сборка

**CI (основной путь):** push в `main` → workflow соберёт библиотеку и выложит артефакт
`libopg3d-armeabi-v7a`, внутри — **только готовая `libopg3d.so`**, больше ничего.

**Локально** (нужен Android NDK):
```bash
cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=armeabi-v7a \
  -DANDROID_PLATFORM=android-21 \
  -DPHOTON_APP_ID="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"   # опционально
cmake --build build -j
```

Опции CMake:

| Опция | По умолчанию | Назначение |
|---|---|---|
| `PHOTON_MODE` | `cloud` | `cloud` — свой Photon Cloud (подмена AppID); `selfhosted` — свой Photon Server |
| `PHOTON_SERVER_ADDRESS` | `""` | Адрес своего сервера (selfhosted или фикс. регион) |
| `PHOTON_SERVER_PORT` | `5055` | Порт сервера (дефолт Photon OnPremise UDP) |
| `SHADOWHOOK_TAG` | `v2.0.1` | Версия ShadowHook |

## Структура

```
├── .github/workflows/build.yml   # CI: armeabi-v7a, артефакт = только libopg3d.so
├── CMakeLists.txt                # сборка + FetchContent ShadowHook
├── src/
│   ├── main.cpp                  # вход: ждём libil2cpp.so + домен, ставим хук
│   ├── photon_hook.cpp           # хук ConnectUsingSettings, перезапись ServerSettings
│   ├── il2cpp.cpp / il2cpp.h     # минимальный il2cpp C-API (resolve через dlsym)
│   ├── config.h                  # конфиг из CMake (без секретов в репо)
│   └── log.h                     # logcat, тег OPG3D
└── LICENSE                       # GPLv3
```

## Отладка

```bash
adb logcat -s OPG3D
```

Каждый шаг (нашёл libil2cpp, домен готов, хук встал, настройки переписаны) пишется в logcat.

## Roadmap

- [x] Анализ IL2CPP-дампа PG3D 12.5.0, поиск точки перехвата
- [x] Библиотека-редиректор (клиентская часть)
- [ ] Способ внедрения: патч APK (`loadLibrary`) / инжектор — на выбор
- [ ] Серверная часть: своё Photon Cloud приложение и/или Photon Server OnPremise
- [ ] Тестирование боя 1х1 на двух устройствах

## Дисклеймер

Фан-проект по ревайвлу мёртвой версии игры. Не аффилирован с Cubic Games. Не чит и не вредит
другим игрокам: онлайн работает только между игроками с этой библиотекой, официальные серверы
версии 12.5.0 недоступны.

## Лицензия

GPLv3 — см. [LICENSE](LICENSE). ShadowHook — MIT (bytedance).
