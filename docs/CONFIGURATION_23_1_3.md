# 23.1.3 feature configuration

All runtime feature switches are grouped at the top of
`opg3d/src/main/cpp/main.cpp` under `feature_config`.

Change a `constexpr bool` from `true` to `false` to exclude that component from
the next build. A disabled component is logged as `disabled` and is treated as
an intentional skip rather than an initialization failure. No implementation
header has to be edited for ordinary component isolation.

## Categories

- `startup`: APK signature compatibility, startup/version traces and guards,
  loading watchdog, and OBB provisioning.
- `network`: local backend, TechnicalWorks suppression, backend emulator,
  Photon route/plugin/trace, network-stall guard, and forced-online state.
- `progression`: the progression driver, currency, XP, fast level-road
  animation, inventory pumps, crafting, catalogue, live-content, modules,
  hidden items, and Pixel Pass.
- `player_content`: local player identity and the bundled assets/data payload.
- `gameplay`: battle UI, rank UI, post-match recovery, and high-tier bots.

## Dependencies

`progression::component` owns the `MainMenuController.Update` driver used by
`inventory_pumps`, `live_content_diagnostics`, and
`pixel_pass_diagnostics`. Turning the progression component off intentionally
stops those pumps even when their module hooks remain enabled. The module
switches can still be disabled independently to isolate their hook sets.

`progression::xp` controls only the level-65 XP grant. It can be disabled while
currency and the rest of the progression driver remain active.
`progression::fast_level_road` controls only the animation-speed hook.
