# 23.1.3 in-battle Armory and rank UI repair

## Scope

This revision repairs two presentation/input defects in the exact supplied
23.1.3 ARM64 client:

1. the Armory entry remains gray during a battle;
2. the mode-selection header renders experience but not the current rank.

It does not change crafting, Photon state, stored experience, or the global
player-level getter.

## Evidence from the device log

The previous online-state revision installed all ten hooks, but none of its
runtime callbacks fired on the gray Armory path. Photon independently reached
`state=9 (Joined)`. The remaining gate is therefore UI state, not internet
reachability.

The same log reported `level=65 exp=999995` before the existing progression
module topped experience up to 900,000,000. Static disassembly confirms that
both rank widgets read that same level:

- `ExperienceController.<level>()` at `0x01C79A50` returns 65;
- `ExpController.<normalizedLevel>()` at `0x027EF518` calls the same getter and
  clamps it to `1..65`;
- `RankIndicatorGuiElement.<refresh>(bool)` at `0x02E78C44` converts that value
  to a string and calls `UILabel.set_text` on `_rankLable` before formatting
  experience.

Consequently, the missing number is a hidden/stale UILabel. It is not evidence
of a second level store and does not justify overriding every level read.

## Rejected candidates

- `ArsenalNestButton`: a timed `Rilisoft.Nest` shop offer, not the battle entry.
- `battle_ui_1610.h`: restores `MainMenuController.multiplayerButton` (the
  lobby's Battle button) in 16.1.0, not an in-match Armory control.
- `AllInterfacePanel.armoryButton`: a serialized reference with no controlling
  method in this build.
- additional generic online verdicts: the device log already disproves them.

## Armory fix

`battle_ui_2313.h` hooks the two actual enable surfaces:

| Surface | RVA | Handling |
| --- | ---: | --- |
| `UIButton.set_isEnabled(bool)` | `0x04E6EC5C` | force `true` only for Armory/Arsenal/Loadout controls |
| `Rilisoft.ButtonHandler.<setEnable>(bool)` | `0x02A2D1E0` | apply the same narrow filter |

There are no direct BL callers for the UIButton setter because NGUI dispatches
it virtually. Hooking the implementation catches those calls without changing
unrelated controls.

A control qualifies only when its GameObject name contains `Armory`, `Arsenal`
or `Loadout`, or when the GameObject was captured from
`PGCompany.UI.UIGotoArmory`. The first 48 disabled-object names are logged but
left stock unless they qualify.

The `UIGotoArmory` setup override is also hooked. At setup time the component's
GameObject is remembered and its attached UIButton/ButtonHandler is enabled
immediately. This covers a button that was disabled before setup and receives
no later setter call. `Component.GetComponent(string)` is the one exact-build
RVA (`0x0443709C`) used here: metadata lookup cannot distinguish it from
`GetComponent(Type)` because both overloads have one parameter.

The stock `UIGotoArmory.U_Click` route remains intact and is only logged. It
continues into `ShopNGUIController` with the original category.

## Rank fix

`rank_ui_2313.h` runs after each stock refresh and repairs only the label:

- `RankIndicatorGuiElement._rankLable` for the mode-selection UI;
- `PlayerPanel.rankLabel` for the legacy/main panel.

The canonical level is read from `ExperienceController`; valid values remain
`1..65`. The label GameObject is activated, the UILabel is enabled, alpha is
set to 1, and text is assigned to that canonical value. The stored 900M XP is
not modified or cosmetically replaced.

## Expected runtime markers

```text
23.1.3-battle-ui: installed 4/4 Armory hooks (...)
23.1.3-battle-ui: captured UIGotoArmory object=... name='...'
23.1.3-battle-ui: UIGotoArmory eager repair #1 attached-control=enabled
23.1.3-battle-ui: restored UIButton Armory control #1 object='...'
23.1.3-battle-ui: Armory click #1 accepted object='...'
23.1.3-rank-ui: installed 2/2 refresh hooks (...)
23.1.3-rank-ui: RankIndicatorGuiElement rank label repaired #1 '...' -> '65' (active=1 enabled=1 alpha=1)
```

Only the applicable Armory restoration path needs to appear: an eager repair
may make a later `restored UIButton` line unnecessary.

## Revision 2: the real 23.1.3 battle prefab

The 2026-08-24 device log showed `UIButton.set_isEnabled(false)` every frame
for exactly one object: `ChangeTeamButton`. It also contained no
`UIGotoArmory` lifecycle/capture marker, so this scene variant cannot be
identified through that component.

`ChangeTeamButton` is now an exact, case-insensitive alias in the same narrow
name filter. A false write to that button is changed to true; unrelated names
and every other disabled button stay stock. The expected marker is now:

```text
23.1.3-battle-ui: kept UIButton Armory control enabled #1 object='ChangeTeamButton'
```
