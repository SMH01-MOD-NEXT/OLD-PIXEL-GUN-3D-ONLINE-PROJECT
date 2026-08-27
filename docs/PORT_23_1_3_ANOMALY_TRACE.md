# 23.1.3 intermittent anomaly comparison trace

This passive trace is intended for side-by-side comparison of a healthy launch
and an affected launch. It does not override any returned value.

It records the three independent offline decisions: the account/settings flag
(`0x2B79FB4`/`0x2B7A00C`), PUN `offlineMode`, and
`OfflineModController.丐一丆丈世丆七丟丘()`. Setter logs contain before,
requested, and after values. Snapshots are also emitted around
`SettingsTabAccount.OnEnable`.

The same stream records all Veteran chest state calculators (`None`, `CanOpen`,
`CantOpen`, `Unavailable`), availability, and `OnEnable`. Existing modules
already trace Auth state/iterator progress, Player ID, Pixel Pass lifecycle,
post-match presentation, bot tier, Photon connection callbacks, and backend
requests. Together these markers provide one chronological comparison chain.

Filter with:

```text
23.1.3-anomaly|23.1.3-auth|23.1.3-local-backend|23.1.3-identity|23.1.3-pixelpass|23.1.3-post-match|23.1.3-bots|23.1.3-photon|23.1.3-backend
```
