# 23.1.3 — unlocking wear (hidden_items v5)

## Symptom

Weapons arrive. The wardrobe does not. The craft screen shows armor, masks,
hats, boots and capes sitting at `0/250` gears — not owned, payable — on an
account that is already level 65 with maxed currency and a complete arsenal.

## Why the sweep skipped them, on purpose

`hidden_items_2313` decides what to grant with two predicates:

```c
inline bool grants_whole_type(int32_t type) {          // v4
    if (g_grant_everything) return true;
    return kGrantEveryWeapon && type == kTypeWeapon;
}

inline bool wants_grant(int32_t type, int32_t offered,
                        const std::string& name) {
    if (grants_whole_type(type)) return true;
    if (is_always_granted(name)) return true;
    return offered == 0;                               // <- wear ended up here
}
```

`offered` is the registry's catalogue lookup 与丅丟七与丌东丙丌(item) at `0x305C6C0`:
non-null means some shop tab, craft list or event list offers this definition.
That filter dates from v2, where its job was to cut ~1500 transactions down to
a few dozen and stop the menu freezing.

A craftable wear piece **is** offered — by the craft list. So `offered == 1`,
`wants_grant` returns false, the definition is counted into `g_offered_skipped`
and deliberately left for the player to pay for. The `0/250` price tag on
screen is not a failed grant; it is the filter working exactly as written.

Weapons stopped going through that line in v3 (`kGrantEveryWeapon`), which is
precisely why the arsenal filled in and the wardrobe did not.

## The change

```c
constexpr bool kGrantEveryWear = true;

inline bool grants_whole_type(int32_t type) {
    if (g_grant_everything) return true;
    if (kGrantEveryWeapon && type == kTypeWeapon) return true;
    if (kGrantEveryWear && is_wear_type(type)) return true;
    return false;
}
```

`is_wear_type` covers the five wear slots of `Rilisoft.OfferItemType`: armor
`20`, mask `30`, hat `40`, boots `50`, cape `60`.

Sweep order becomes weapon, armor, mask, hat, boots, cape, gadget, skin, so
both bulk-granted groups land first.

Nothing else in the mechanism changes: the same stock grant entry point
(`0x3062B08`), the same owned-check before and after every grant, the same v4
pacing, the same fail-closed image verification.

## What it costs

Wear is a wider catalogue than weapons — five types, much of it seasonal — so
the bulk pass grows from roughly 800 transactions to a few thousand. At the
15-40 ms per transaction v4 measured, expect on the order of ten seconds of
reduced frame rate on the main menu instead of a few seconds.

One thing gets *cheaper* per definition: a type granted whole needs no
catalogue lookup, because the answer cannot change the decision. In v4 every
wear definition paid for an `is_offered()` call whose only effect was to skip
it.

Read the real numbers rather than trusting that estimate:

```sh
adb logcat -s OPG3D | grep -E '23\.1\.3-hidden-items: (progress|pass|armor|mask|hat|boots|cape)'
```

The per-type lines report `definitions / already owned / wanted / granted`. A
wear type that finishes with `wanted=0` while items are still priced on the
craft screen means those definitions live under a type id this table does not
sweep — not that the grant failed.

## Deliberately left alone

| Not included | Why |
| --- | --- |
| Gadgets (`70`) | Not requested, and the gadget catalogue is where the one-shot id dump is still hunting the harpoon id. One-line flip if wanted. |
| Skins (`65`) | Still behind `kIncludeSkins = false`. A much larger cosmetic catalogue; enabling it would multiply the bulk pass again. |
| Pets, modules | Not swept here. `weapon_modules_2313` owns modules. |

## Untested on device

Written from the metadata dump and the shipped v4 source. Not built and not
run: the timings above are arithmetic from v4's measured per-transaction cost,
not observations. The progress log is what settles it.
