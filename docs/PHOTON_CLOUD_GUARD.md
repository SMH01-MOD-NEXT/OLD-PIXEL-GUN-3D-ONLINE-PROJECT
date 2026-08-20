# Photon Cloud routing and dead-backend guard

## Подтверждённые причины проблем подключения

### Мёртвый backend и локальный Disconnect

Расширенная трассировка на 12.5.0 показала одну и ту же цепочку на каждой
попытке:

```text
ConnectUsingSettings -> StatusCode.Connect -> ClientState.Authenticating
-> FriendsController.Update -> PhotonNetwork.Disconnect
```

На 12.5.0 call-site был определён по `dump.cs` из runtime-адреса
`libil2cpp.so+0xAA23FC` как `FriendsController.Update+0x310`. Абсолютные
смещения относятся к конкретной сборке; в 13.2.1 они иные, поэтому guard
ставит хук на метод `FriendsController.Update` целиком и от смещения не
зависит. Это локальный вызов игры, а не разрыв со стороны Photon.

Одновременно на 12.5.0 `ServerSettings` после `Switcher.SetUpPhoton` оставался
в режиме `SelfHosted` с мёртвым адресом `rilisoft-us.exitgamescloud.com:5055`.
Дефолтная конфигурация 13.2.1 также содержит непригодный старый маршрут.

### Cold start BestRegion в 13.2.1

На первом запуске путь `UseCloudBestRegion` выполнял асинхронный выбор региона.
До завершения ping-корутины и записи `PUNCloudBestRegion` в PlayerPrefs клиент
успевал отправить Authenticate с `region=none`:

```text
photon-op: code=220 return=0
photon-op: code=230 return=32756
             debug='Cloud public / Region none is not available.'
```

Примерно через 20–30 секунд лучший регион сохранялся, очередная попытка
подключалась успешно, а следующие запуски уже использовали кэш. Поэтому ошибка
«проверьте подключение» проявлялась только на первых запусках.

В `dump1321.cs` подтверждено:

```text
ServerSettings.HostingOption.PhotonCloud = 1
CloudRegionCode.eu = 0
CloudRegionCode.none = 4
```

## Что делает guard

1. Перед каждым известным входом в подключение вызывает штатный overload PUN
   `ServerSettings.UseCloud(PHOTON_APP_ID, CloudRegionCode.eu)`.
2. Проверяет read-back трёх инвариантов: `HostType == PhotonCloud (1)`,
   `PreferredRegion == eu (0)` и сохранён тот же AppID. Если SDK-метод
   недоступен или результат перезаписан, выполняется ограниченный field
   fallback. Ложный успех не объявляется.
3. Пока PUN находится в активном или переходном состоянии, не запускает
   `FriendsController.Update`. Это изолирует только владельца мёртвого
   HTTP/social backend и гарантированно убирает подтверждённый call-site
   Disconnect. В состоянии `PeerCreated`/`Disconnected` оригинальный Update
   продолжает работать для локальной инициализации.
4. Ручной `PhotonNetwork.Disconnect`, серверные причины Disconnect, Photon
   callbacks, комнаты, RPC и синхронизация не подменяются.

Фиксированный EU убирает cold-start ping/cache и гарантирует, что все клиенты
с одним AppID и AppVersion попадают в один региональный пул комнат. На стороне
Photon Cloud для этого приложения следует оставить EU единственным allowed
region.

## Ожидаемые логи

Перед подключением:

```text
trace: ServerSettings.UseCloud(region=0) ...
settings[UseCloud(region)/end]: host=1(PhotonCloud) ... region=0 ...
cloud-force[...]: ... host=1(PhotonCloud expected=1) region=0(eu expected=0) ... ready=1
```

Во время handshake допустимы строки:

```text
backend-guard: skipped FriendsController.Update ... state=20(Authenticating)
```

Authenticate должен завершиться без промежуточной ошибки региона:

```text
photon-op: code=230 return=0
ui: ConnectionControl.OnConnectedToMaster ...
```

`code=220` (GetRegions) больше не требуется для обычного подключения, а ответ
`32756 / Region none` после этого изменения считается регрессией.

Старое значение `ServerAddress` может оставаться сериализованным в asset, но
при `HostType=PhotonCloud` ветка SelfHosted его не использует.
