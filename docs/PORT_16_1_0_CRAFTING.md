# 16.1.0 local upgrade, weapon craft, clan craft and lobby catalogue port

This patch targets the supplied ARMv7 `libil2cpp.so` (SHA-256
`2aab620cb58a597e86975a78ab20987e71685b507456707ed42fa63fad54032b`).
The repository branch remains `16.1.1-test`; the branch and workflow names are
intentionally unchanged.

## Upgrade-time fix

The weapon upgrade flow calls the obfuscated 16.1.0 equivalent of
`FriendsController.get_ServerTime()` (`不丕专丈丄丄丆丞七/0`, RVA
`0x006C5288`). When the retired backend returns no time, the hook supplies local
UTC Unix seconds and keeps the value monotonic. The stock upgrade/save path is
otherwise unchanged.

## Zero-detail weapon crafting

The three independent inputs used by the 16.1.0 armory are overridden:

- `BalanceController.丕东丁丌丞丂丕丕与/1` -> required details `0`.
- `Rilisoft.不丅专丆与且丛丅丑.三业丛丘丁丝万丛丞/2` -> enough details.
- `Rilisoft.不丅专丆与且丛丅丑.丛丆万七丘丈业丅丏/1` -> owned details
  `99999` when the real value is lower.

The first and third mappings were cross-checked by ARM BL callers: the gate
calls both methods, and the armory/detail views call the owned-count method.

## Dead-clan workaround

The patch makes the craft section available, sets clan medal cost to zero, and
supplies read-only synthetic clan stock without creating a fake `Clan` object.
If the stock craft-button handler raises the dead-clan hint, that hint is
suppressed and the exact stock success sequence is reproduced:

1. obfuscated `ItemDb.GetByTag(itemId)`;
2. obfuscated item-record prefab getter;
3. `BalanceController.GetFullTimeCraftInSeconds(prefab)`;
4. monotonic local server time;
5. `WeaponManager.StartCraftWeaponOrAvatar(itemId, endTime)`.

The current craft slot is checked before and after the call, so an active craft
is never overwritten and acceptance is verified.

## Lobby catalogue

`LobbyItemsController.Update()` waits for `get_IsReady`, scans `get_AllItems`
in small batches, skips owned items, and calls the stock 16.1.0
`AddItemNow/5`. Bulk grants use `autoEquip=false`, retain normal grant behavior,
pass a null optional price descriptor (a stock 16.1.0 call site does the same),
and retain the final save flag. No ownership arrays are edited directly.

## Required device checks

Capture `adb logcat -s OPG3D` and verify:

1. startup reports `craft-16.1.0: ... armed` and
   `lobby-16.1.0: ... armed`;
2. upgrading a weapon no longer shows the connection error and survives a
   restart;
3. a regular detail weapon shows `0` required, crafts, finishes, and survives a
   restart;
4. a clan blueprint crafts without joining a clan; pressing while another
   craft is active does not overwrite the slot;
5. lobby items are granted over multiple frames, do not replace the currently
   equipped lobby item, and remain owned after restart.
