# OLD-PIXEL-GUN-3D-ONLINE-PROJECT
Experiment with AI: creating the private server for pg3d old version (12.5.0) arm-v7

# NOTE: We DON'T CREATE A CHEAT OR SOMETHING ILLEGAL
We just need to make library that can be loaded to the game, functional: ONLY redirecting online battes to MINE photon server, NOT OFFICIAL. Official servers already dead because of old version, so we CAN'T make harm things for other players and NOT PLANNING to do this.

# We need to make online working and make Pixel Gun Great Again. Happy coding!

---

## Как это работает

Нативная библиотека `libopg3d.so` (C++17, armeabi-v7a, **без единой внешней зависимости**)
загружается в процесс игры и переписывает поля статического объекта
`PhotonNetwork.PhotonServerSettings` — того самого, откуда PUN classic (~1.79) берёт
все данные при подключении:

| Поле | Смещение | Что делаем |
|---|---|---|
| `HostType` | `0x0C` | `PhotonCloud` (1) или `SelfHosted` (2) — по режиму сборки |
| `Protocol` | `0x10` | `Udp` (0) в режиме selfhosted |
| `ServerAddress` / `ServerPort` | `0x14` / `0x18` | свой сервер (selfhosted / фикс. регион) |
| `AppID` / `VoiceAppID` | `0x1C` / `0x20` | AppID **своего** Photon Cloud приложения |

Смещения восстановлены из `dump.cs` (IL2CPP metadata v22) и сверены с `libil2cpp.so`.

Порядок работы (фоновый поток, стартует из `__attribute__((constructor))`):

1. ждём, пока в процессе появится `libil2cpp.so`;
2. резолвим экспорты `il2cpp_*` сами: `dl_iterate_phdr` → `PT_DYNAMIC` → `DT_SYMTAB`/`DT_STRTAB`/`DT_HASH`
   → линейный проход по `.dynsym` (всего ~741 символ). `dlsym` тут бесполезен из-за
   linker namespace'ов Android 7+;
3. ждём IL2CPP-домен и `Assembly-CSharp.dll`, присоединяем свой поток к рантайму
   (`il2cpp_thread_attach` — без этого нельзя создавать managed-строки);
4. сторожим настройки: как только игра создаст объект — пишем свои значения,
   и возвращаем их, если игра перезагрузит ассет.

## Почему без инлайн-хука и без зависимостей

Изначально библиотека хукала `PhotonNetwork.ConnectUsingSettings` через
[ShadowHook](https://github.com/bytedance/android-inline-hook). На реальном устройстве
`shadowhook_init()` падал **ещё до первого хука**, причём дважды подряд:

- **ошибка 8 — `Init bytesig mod SIGSEGV failed`.** `bytesig` ставит SIGSEGV-хендлер с флагом
  `SA_EXPOSE_TAGBITS`, который ядро отклоняет (`EINVAL`) в 32-битных процессах — там нет
  tagged addresses. Апстрим: [#78](https://github.com/bytedance/android-inline-hook/issues/78);
- **ошибка 12 — `Init linker mod failed`.** ShadowHook ищет внутренние символы динамического
  линкера (`soinfo::call_constructors` и т.п.) и, не найдя их на конкретной прошивке,
  валит инициализацию целиком. Апстрим: [#113](https://github.com/bytedance/android-inline-hook/issues/113),
  [#91](https://github.com/bytedance/android-inline-hook/issues/91).

Ключевой вывод: **инлайн-хук для этой задачи не нужен вообще.** Мы не меняем код игры,
а только данные — значит не нужны ни перехват функций, ни патчинг линкера,
ни перехват сигналов. Побочные плюсы: сборка полностью оффлайновая (нет FetchContent),
библиотека маленькая и самодостаточная, зависимость от версии Android минимальная.

Сознательно **не** вызываем `il2cpp_runtime_class_init` для `PhotonNetwork`: в его статическом
конструкторе идёт `Resources.Load`, а Unity запрещает это из фонового потока — типизированное
исключение сломало бы Photon навсегда. Ждём, пока игра всё инициализирует сама.

## AppID — НЕ хардкодим

AppID — это креды, в репозитории его нет и не будет. Передаётся только на сборке:

- **CI:** GitHub → **Settings → Secrets and variables → Actions → New repository secret** →
  имя `PHOTON_APP_ID` → workflow подхватывает его через `ORG_GRADLE_PROJECT_PHOTON_APP_ID`.
- **Локально:** `gradle :opg3d:assembleRelease -PPHOTON_APP_ID=xxxxxxxx-xxxx-...`

Сборка без секрета тоже работает: библиотека компилируется и запускается, но AppID
не меняется (passthrough + warning в logcat). Удобно для форков и отладки.

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
│           ├── CMakeLists.txt                           # без внешних зависимостей, оффлайн
│           ├── main.cpp                                 # вход: ожидание, резолв, сторож
│           ├── elf_sym.cpp / elf_sym.h                  # свой резолвер символов по .dynsym
│           ├── il2cpp.cpp / il2cpp.h                    # минимальный il2cpp C-API
│           ├── photon_patch.cpp / photon_patch.h        # подмена полей ServerSettings
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
Сеть при сборке нативной части не нужна вообще — ничего не скачивается.

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
init: экспорты il2cpp найдены
init: рантайм готов, поток присоединён
patch: режим Cloud, штатный Name Server, меняем только AppID
patch: AppID подменён (длина 36)
init: всё готово — настройки Photon подменены
```

Если что-то не так — одно понятное сообщение по этапу: либа не найдена /
экспорты не найдены / домен не поднялся / thread_attach не сработал /
класс PhotonNetwork не найден. Строка `watch: ждём, когда игра создаст
ПhotonServerSettings` — норма: значит дошли до сторожа и ждём инициализации Photon игрой.

## Roadmap

- [x] Анализ IL2CPP-дампа PG3D 12.5.0, поиск точки вмешательства
- [x] Библиотека-редиректор (клиентская часть)
- [x] Отказ от ShadowHook: свой резолвер `.dynsym` + подмена данных вместо хука
      (лечит ошибки 8 и 12, убирает все внешние зависимости)
- [ ] Способ внедрения: патч APK (`loadLibrary`) / инжектор — на выбор
- [ ] Серверная часть: своё Photon Cloud приложение и/или Photon Server OnPremise
- [ ] Тестирование боя 1х1 на двух устройствах

## Дисклеймер

Фан-проект по ревайвлу мёртвой версии игры. Не аффилирован с Cubic Games. Не чит и не вредит
другим игрокам: онлайн работает только между игроками с этой библиотекой, официальные серверы
версии 12.5.0 недоступны.

## Лицензия

GPLv3 — см. [LICENSE](LICENSE). Весь код свой, сторонних зависимостей нет.
