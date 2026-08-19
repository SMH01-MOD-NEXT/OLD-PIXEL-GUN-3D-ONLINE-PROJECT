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
(thumb-бит при хуке не нужен). Весь используемый il2cpp C-API экспортирован игрой;
символы резолвятся через `shadowhook_dlopen`/`shadowhook_dlsym` (линкерные namespace'ы
Android 7+ пробиваем напрямую — `dlsym(RTLD_DEFAULT)` игрушечную либу не видит).

## Почему shadowhook собирается из исходников, а не из prefab/AAR

В официальной либе bytesig (crash-protection) ставит SIGSEGV-хендлер с флагом
`SA_EXPOSE_TAGBITS`. Ядро отклоняет `sigaction()` с этим флагом (`EINVAL`) в процессах без
tagged addresses — а 32-битный процесс (наш случай, armeabi-v7a) их не имеет по определению.
Итог: `shadowhook_init()` падает с ошибкой **8 (Init bytesig mod SIGSEGV failed)**.
Апстрим-баг, открыт: [android-inline-hook#78](https://github.com/bytedance/android-inline-hook/issues/78).

Лечение — патч исходников при сборке: [`opg3d/src/main/cpp/cmake/patch-bytesig.cmake`](opg3d/src/main/cpp/cmake/patch-bytesig.cmake)
убирает этот флаг (на crash-protection не влияет — флаг нужен только для MTE-диагностики).
Заодно: shadowhook линкуется **статически**, поэтому на выходе одна самодостаточная `libopg3d.so`,
без сопутствующих `.so`.

## AppID — НЕ хардкодим

AppID — это креды, в репозитории его нет и не будет. Передаётся только на сборке:

- **CI:** GitHub → **Settings → Secrets and variables → Actions → New repository secret** →
  имя `PHOTON_APP_ID` → workflow подхватывает его через `ORG_GRADLE_PROJECT_PHOTON_APP_ID`.
- **Локально:** `gradle :opg3d:assembleRelease -PPHOTON_APP_ID=xxxxxxxx-xxxx-...`

Сборка без секрета тоже работает: библиотека компилируется и хук встаёт, но AppID не меняется
(passthrough + warning в logcat). Удобно для форков и отладки хука.

Прочие gradle-свойства (опционально): `PHOTON_MODE` (`cloud` по умолчанию / `selfhosted`),
`PHOTON_SERVER_ADDRESS`, `PHOTON_SERVER_PORT` (5055).

## Структура

```
├── settings.gradle / build.gradle / gradle.properties   # корневой Gradle-проект (AGP 8.7.3)
├── opg3d/                                               # Android library-модуль
│   ├── build.gradle                                     # ABI armeabi-v7a, конфиг -> CMake
│   └── src/main/
│       ├── AndroidManifest.xml
│       └── cpp/
│           ├── CMakeLists.txt                           # FetchContent shadowhook + патч + сборка
│           ├── cmake/patch-bytesig.cmake                # фикс ошибки 8 (см. выше)
│           ├── main.cpp                                 # вход: ждём libil2cpp.so, резолв, хук
│           ├── photon_hook.cpp / photon_hook.h          # хук ConnectUsingSettings
│           ├── il2cpp.cpp / il2cpp.h                    # минимальный il2cpp C-API
│           ├── config.h                                 # конфиг (без секретов в репо)
│           └── log.h                                    # logcat, тег OPG3D
├── .github/workflows/build.yml                          # CI: gradle assembleRelease
└── LICENSE                                              # GPLv3
```

## Сборка

**IDE (основной путь):** открыть корень репозитория в Android Studio → Gradle Sync →
Build → Assemble ':opg3d'. Готовая либа: `opg3d/build/intermediates/.../armeabi-v7a/libopg3d.so`.

**CLI:** нужны JDK 17, Android SDK, NDK `27.3.13750724`, CMake `3.31.5` (из SDK):
```bash
gradle :opg3d:assembleRelease -PPHOTON_APP_ID="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
```
(нет локального gradle — один раз `gradle wrapper` или просто собирайте из Android Studio.)

**CI (GitHub Actions):** push в `main` → workflow собирает и выкладывает артефакт
`libopg3d-armeabi-v7a` — внутри **только готовая `libopg3d.so`**, больше ничего.

## Отладка

```bash
adb logcat -s OPG3D
```

Логи идут по одному сообщению на смену состояния (без спама):

```
init: поток запущен
init: libil2cpp.so найдена, base = 0x...
init: il2cpp API готов
init: хук установлен: ConnectUsingSettings @ 0x...
init: всё готово, ждём вызова ConnectUsingSettings
```

Если что-то не так — одно понятное сообщение по этапу (либа не найдена / нет handle /
символы не зарезолвились / домен не поднялся / хук не встал + errno shadowhook).
У самого shadowhook свой тег `shadowhook` (включить: `shadowhook_init(..., true)` в main.cpp).

## Roadmap

- [x] Анализ IL2CPP-дампа PG3D 12.5.0, поиск точки перехвата
- [x] Библиотека-редиректор (клиентская часть)
- [x] Фикс инициализации shadowhook на 32-бит (патч bytesig, апстрим #78)
- [ ] Способ внедрения: патч APK (`loadLibrary`) / инжектор — на выбор
- [ ] Серверная часть: своё Photon Cloud приложение и/или Photon Server OnPremise
- [ ] Тестирование боя 1х1 на двух устройствах

## Дисклеймер

Фан-проект по ревайвлу мёртвой версии игры. Не аффилирован с Cubic Games. Не чит и не вредит
другим игрокам: онлайн работает только между игроками с этой библиотекой, официальные серверы
версии 12.5.0 недоступны.

## Лицензия

GPLv3 — см. [LICENSE](LICENSE). ShadowHook — MIT (bytedance), его исходники скачиваются
при сборке (FetchContent, тег v2.0.1) и патчатся локально.
