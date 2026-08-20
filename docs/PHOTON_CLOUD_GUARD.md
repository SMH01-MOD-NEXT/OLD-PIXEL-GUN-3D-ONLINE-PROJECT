# Photon Cloud routing and dead-backend guard

## Подтверждённая причина цикла

Расширенная трассировка показала одну и ту же цепочку на каждой попытке:

```text
ConnectUsingSettings -> StatusCode.Connect -> ClientState.Authenticating
-> FriendsController.Update+0x310 -> PhotonNetwork.Disconnect
```

`FriendsController.Update+0x310` определён по `dump.cs` из runtime call-site
`libil2cpp.so+0xAA23FC`. Это локальный вызов игры, а не разрыв со стороны
Photon.

Одновременно `ServerSettings` после `Switcher.SetUpPhoton` оставался в режиме
`SelfHosted` с адресом `rilisoft-us.exitgamescloud.com:5055`. Режим сборки
`PHOTON_MODE=cloud` раньше менял только AppID и ошибочно сохранял выбранный
игрой маршрут.

## Что делает guard

1. Перед каждым известным входом в подключение вызывает штатный метод PUN
   `ServerSettings.UseCloudBestRegion(PHOTON_APP_ID)`.
2. Проверяет, что `HostType == BestRegion (4)`. Если SDK-метод недоступен или
   не изменил режим, выполняет ограниченный fallback записи `HostType` и
   `AppID`; ложный успех не объявляется.
3. Пока PUN находится в активном или переходном состоянии, не запускает
   `FriendsController.Update`. Это изолирует только владельца мёртвого
   HTTP/social backend и гарантированно убирает подтверждённый call-site
   Disconnect. В состоянии `PeerCreated`/`Disconnected` оригинальный Update
   продолжает работать для локальной инициализации.
4. Ручной `PhotonNetwork.Disconnect`, серверные причины Disconnect, Photon
   callbacks, комнаты, RPC и синхронизация не подменяются.

## Ожидаемые логи

Перед подключением:

```text
trace: ServerSettings.UseCloudBestRegion ...
settings[UseCloudBestRegion/end]: host=4(BestRegion) ...
cloud-force[...]: ... host=4 ... ready=1
```

Во время handshake допустимы строки:

```text
backend-guard: skipped FriendsController.Update ... state=20(Authenticating)
```

После этого ожидается `photon-op` для Authenticate/GetRegions и переход в
`ConnectedToMaster`, `Authenticated` или `JoinedLobby` вместо локального
Disconnect.

Старое значение `ServerAddress` может оставаться сериализованным в asset, но
при `HostType=BestRegion` ветка SelfHosted его не использует.
