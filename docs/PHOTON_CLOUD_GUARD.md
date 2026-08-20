# Photon Cloud routing and dead-backend guard

## Подтверждённая причина цикла

Расширенная трассировка 12.5.0 показала одну и ту же цепочку на каждой
попытке:

```text
ConnectUsingSettings -> StatusCode.Connect -> ClientState.Authenticating
-> FriendsController.Update+0x310 -> PhotonNetwork.Disconnect
```

`FriendsController.Update+0x310` определён по `dump1250.cs` из runtime
call-site `libil2cpp.so+0xAA23FC`. Это локальный вызов игры, а не разрыв со
стороны Photon.

Одновременно `ServerSettings` после `Switcher.SetUpPhoton` оставался в режиме
`SelfHosted` с адресом `rilisoft-us.exitgamescloud.com:5055`. Режим сборки
`PHOTON_MODE=cloud` раньше менял только AppID и ошибочно сохранял выбранный
игрой маршрут.

## Фиксированный регион

`dump1250.cs` подтверждает:

```text
ServerSettings.HostingOption.PhotonCloud = 1
CloudRegionCode.eu = 0
CloudRegionCode.none = 4
ServerSettings.UseCloud(string, CloudRegionCode) // RVA 0x8FF47C
```

`BestRegion` не используется: он зависит от асинхронного ping/cache и может
как задержать первый connect, так и распределить клиентов по разным
региональным пулам комнат. Для общего онлайна 12.5.0 все клиенты фиксируются
на EU. На стороне Photon Cloud для соответствующего AppID также следует
оставить EU единственным allowed region.

## Что делает guard

1. Перед каждым известным входом в подключение вызывает штатный overload PUN
   `ServerSettings.UseCloud(PHOTON_APP_ID, CloudRegionCode.eu)`.
2. Проверяет read-back трёх инвариантов: `HostType == PhotonCloud (1)`,
   `PreferredRegion == eu (0)` и сохранён тот же AppID.
3. Если метод отсутствует или read-back не совпал, подключение блокируется.
   Guard не угадывает offsets и не объявляет ложный успех.
4. Пока PUN находится в активном или переходном состоянии, не запускает
   `FriendsController.Update`. Это изолирует только владельца мёртвого
   HTTP/social backend и гарантированно убирает подтверждённый call-site
   Disconnect. В состоянии `PeerCreated`/`Disconnected` оригинальный Update
   продолжает работать для локальной инициализации.
5. Ручной `PhotonNetwork.Disconnect`, серверные причины Disconnect, Photon
   callbacks, комнаты, RPC и синхронизация не подменяются.

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

Затем ожидаются успешный Authenticate и переход в master/game server:

```text
photon-op: code=230 return=0
ui: ConnectionControl.OnConnectedToMaster ...
```

Старое значение `ServerAddress` может оставаться сериализованным в asset, но
при `HostType=PhotonCloud` ветка SelfHosted его не использует.
