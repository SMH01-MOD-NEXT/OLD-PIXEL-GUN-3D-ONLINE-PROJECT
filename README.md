# OLD PIXEL GUN 3D ONLINE PROJECT

Открытый GPLv3-проект по восстановлению онлайна Pixel Gun 3D 12.5.0
(`armeabi-v7a`) через **своё** приложение Photon Cloud. Это не чит: библиотека
не подключает игрока к официальному онлайну и не меняет игровой баланс.

## Что исправляет библиотека

В 12.5.0 игра не просто читает `PhotonNetwork.PhotonServerSettings.AppID`.
При запуске вызывается следующая цепочка:

```text
Switcher.SetUpPhoton(HiddenSettings)
  -> Switcher.SelectPhotonAppId(HiddenSettings)
  -> ServerSettings.UseCloud(selectedAppId)
```

`HiddenSettings` содержит кодированные варианты AppID, pad/signature и «нулевой»
вариант. `SelectPhotonAppId` учитывает локальное значение из `Storager` и хеш
подписи APK. Поэтому поздняя запись в `PhotonServerSettings.AppID` перетиралась
самой игрой и была недостаточна.

Новая реализация перехватывает **источник значения** —
`Switcher.SelectPhotonAppId`. При наличии `PHOTON_APP_ID` исходная ветка выбора
(включая kill-switch/signature path) не выполняется, а игре возвращается AppID
своего Photon Cloud приложения. Затем обычная сетевая логика PUN продолжает
работать штатно.

Мы **не** форсим глобальные ответы `Storager.getInt`, не подменяем посторонние
настройки и не загружаем закрытую референсную библиотеку.

## Независимая open-source реализация

- весь код редиректора написан с нуля и опубликован в этом репозитории;
- сторонний бинарь, использованный для анализа поведения, не включён, не
  исполняется, не линкуется и не распространяется проектом;
- методы находятся через официальный экспортируемый IL2CPP metadata API
  (`class_get_method_from_name` → `MethodInfo::methodPointer`), а не через
  хардкод абсолютных адресов;
- если обязательный метод не найден, патч **fail-closed** и не пишет по
  предположительному RVA;
- ARM32 inline-hook выполняет [Dobby](https://github.com/jmpews/Dobby),
  статически собранный из зафиксированного commit
  `5dfc8546954ce3b3198132ab13fddb89ee92cdd7` (Apache-2.0);
- ShadowHook и его linker/bytesig-модули не используются.

Dobby нужен только как небольшой механизм перенаправления ARM-кода. Его shared
library не попадает в результат: в `libopg3d.so` статически встраивается
`dobby_static`, поэтому конечный артефакт по-прежнему состоит из **одного файла**.
Сведения о лицензии: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Фаза 0: полная диагностика подключения

Кроме обязательной подмены источника AppID устанавливаются диагностические хуки:

- `Switcher.SetUpPhoton` и все варианты `ServerSettings.UseCloud/UseMyServer`;
- `PhotonNetwork.ConnectUsingSettings`, `ConnectToMaster`, `Disconnect`;
- `GameConnect.GetConnectGameVersion` и `GameConnect.ConnectToPhoton`;
- `NetworkingPeer.DebugReturn`, `OnStatusChanged`, `OnOperationResponse`;
- `ConnectionControl.OnFailedToConnect`, `OnDisconnected`,
  `OnConnectedToMaster`.

Включаются `PhotonLogLevel.Full` и `PhotonPeer.DebugOut = ALL`. Логи показывают:

- реальный путь подключения и числовые состояния клиента;
- адрес/порт, протокол, регион и AppVersion матчмейкинга;
- operation code, return code и debug message ответов Photon;
- причины disconnect/authentication failure;
- наличие `AuthValues`, токена и UserId **без вывода их содержимого**;
- AppID только как длину и FNV-1a fingerprint — credential целиком никогда не
  попадает в logcat.

Просмотр:

```bash
adb logcat -s OPG3D
```

Ключевые строки успешной установки:

```text
init: IL2CPP API resolved
hook: installed Switcher.SelectPhotonAppId/1
hook: installed ... managed hooks (core=OK)
init: phase 0 ready — AppID selection override and connection tracing active
appid: SelectPhotonAppId #1 -> configured AppID {len=36 fnv1a=...}
net: ConnectUsingSettings begin ...
photon-status: 1024 (Connect) ...
```

## AppID не хранится в Git

`PHOTON_APP_ID` является credential. В исходниках и истории Git его быть не
должно.

### GitHub Actions

Создай repository secret:

```text
Settings -> Secrets and variables -> Actions -> PHOTON_APP_ID
```

Workflow передаёт его как `ORG_GRADLE_PROJECT_PHOTON_APP_ID`. GitHub маскирует
значение в build log, а workflow публикует артефакт
`libopg3d-armeabi-v7a`, внутри которого только `libopg3d.so`.

### Локальная сборка

Нужны JDK 17, Android SDK, NDK `27.3.13750724`, CMake `3.31.5`:

```bash
gradle :opg3d:assembleRelease \
  -PPHOTON_APP_ID="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
```

Проект можно открыть корнем в Android Studio. ABI зафиксирован на
`armeabi-v7a`. Во время первого configure CMake скачает ровно зафиксированный
исходник Dobby; после этого он остаётся в Gradle/CMake cache.

Сборка без секрета допустима: все диагностические хуки ставятся, но
`SelectPhotonAppId` работает в passthrough-режиме и пишет предупреждение.

Опциональные свойства (экспериментально):

- `PHOTON_MODE=cloud` (по умолчанию) или `selfhosted`;
- `PHOTON_SERVER_ADDRESS`;
- `PHOTON_SERVER_PORT` (по умолчанию `5055`).

## Структура

```text
opg3d/src/main/cpp/
├── main.cpp                  # ожидание IL2CPP, attach, установка хуков
├── elf_sym.cpp/.h            # поиск экспортов libil2cpp.so в памяти
├── il2cpp.cpp/.h             # metadata API, managed strings/fields
├── hook.cpp/.h               # fail-closed Dobby wrapper
├── photon_hooks.cpp/.h       # AppID override + сетевой trace
├── config.h                  # compile-time defaults без credential
└── CMakeLists.txt            # pinned Dobby static + libopg3d.so
```

## Ограничения текущей стадии

- целевая сборка: PG3D 12.5.0, Android ARMv7, IL2CPP metadata v22;
- первая цель — доказать успешный Photon handshake и точно увидеть оставшиеся
  отказы мёртвого backend;
- HTTP/backend-эмуляция и manual connect будут добавляться только если новые
  логи покажут, что корректного AppID и штатного PUN-пути недостаточно;
- способ загрузки `libopg3d.so` в процесс игры не входит в эту стадию.

## Roadmap

- [x] Разбор IL2CPP 12.5.0 и фактической цепочки выбора AppID
- [x] Независимый hook `SelectPhotonAppId` без Storager-костылей
- [x] Полная трассировка PUN/Photon connection path
- [ ] Тест Photon handshake на устройстве
- [ ] Обход конкретных мёртвых backend-гейтов — только по подтверждённому логу
- [ ] Тест комнаты/боя 1×1 на двух устройствах

## Лицензия и дисклеймер

Собственный код проекта — GPLv3, см. [LICENSE](LICENSE). Dobby — Apache-2.0.
Проект не аффилирован с Cubic Games/Photon и предназначен для совместимого
фанатского онлайна уже отключённой версии игры на инфраструктуре владельца
сборки.
