# 23.1.3 experiment: 24.0.1 resources on a 23.1.3 client

This branch exists for one question only: what happens when the shipped 23.1.3
asset-bundle Downloader is pointed at the 24.0.1 resource set? It is an
experiment, not a feature, and it is deliberately kept out of the port branch.

## How 23.1.3 resolves its resources

Everything below lives in `PGCompany.AssetBundles_v3` in the 23.1.3 il2cpp
image. RVAs are for the exact 23.1.3 ARM64 `libil2cpp.so`
(build id `57fcc18d2db06212416d480d53c0f881ee47c52a`).

| Step | Member | RVA |
| --- | --- | --- |
| Which bundle config to read | url helper, `ConfigId` picker, 0 args | `0x1D25128` |
| Config payload to bundle list | url helper, `string -> List<name+hash>` | `0x1D36CDC` |
| Per-platform resource path | url helper, `(platform, string) -> string` | `0x1D39130` |
| Per-bundle download URL | url helper, `name+hash -> Uri` | `0x1D35758` |
| Queue hand-off to the native downloader | `AndroidNativeAssetBundleDownloader.SetLoadingQueue` | `0x1D2795C` |
| Bundle name / hash accessors | name+hash holder getters | `0x1D2A674` / `0x1D2A728` |

Relevant constants inside the url helper: `AssetBundlesIos = 152`,
`AssetBundlesAndroid = 153`, CDN host `pixelgun3d.akamaized.net`, LAN debug host
`192.168.180.249`. The URL itself is composed with `UriBuilder(scheme, host)` and
`set_Path`, and the actual download is performed by the Java bridge
`com.lightmap.assetbundledownload.Bridge` through
`AndroidNativeAssetBundleDownloader`.

The important consequence: **bundles are content addressed by name and hash, and
the name/hash list comes from the version-keyed config, not from the binary.**
There is no hardcoded `23.1.3` in any download URL, so there is nothing to
"bump" blindly - the only lever that decides which resource generation is
fetched is the payload the config layer hands to the parser.

## What this module does

`opg3d/src/main/cpp/res_2401_experiment_2313.{h,cpp}` installs five hooks:

1. **ConfigId picker** - read only. Logs whether this device asks for 152 or 153.
2. **Config payload parser** - logs the payload (size, ASCII flag, first 400
   characters) and swaps the `23.1.3` token for `24.0.1` when the payload
   actually carries one. Optionally appends explicit `<name>_<hash>` entries.
3. **Per-bundle Uri factory** - read only. Logs `bundle '<name>' hash '<hash>'
   -> <url>` so the exact CDN request the old client makes is visible.
4. **Per-platform resource path helper** - same token swap, applied to the
   returned string.
5. **`SetLoadingQueue`** - read only. Logs the final queue size right before the
   native downloader starts working.

Every rewrite is fail-open and length-checked:

- if the version token is not present, nothing is modified;
- if `il2cpp_string_new` fails or the rewritten managed string does not have the
  expected length, the original string is passed through;
- payloads larger than 400000 characters, or payloads that are not pure ASCII,
  are never rewritten (a truncated payload would silently drop bundles).

So the worst realistic case of the default configuration is "nothing changes and
the log tells us why", not a broken client.

## What is deliberately not touched

`UpdatesChecker` (TypeDefIndex 6204), its nested coroutines (6201-6203),
`ClientUpdateBannerWindow` (TypeDefIndex 2099) and `UpdateIdBindingWindow`
(TypeDefIndex 6200) are **not** referenced, resolved or patched by this module.
Hooking that class prevents 23.1.3 from starting, and the update-banner
suppression that already exists in the port stays exactly as it is.

The module also does not spoof the global application version. A global version
spoof would leak into login, matchmaking and analytics; this experiment stays
inside the resource layer.

## Wiring

The module installs itself from its own translation unit (the CMake target globs
`*.cpp`), so `main.cpp` is not modified on this branch and the port's startup
reporting is unchanged. `install_hooks()` is idempotent and is retried while
IL2CPP metadata settles, which lets it arm before the first bundle request
instead of waiting for the regular module bootstrap.

## What to expect on device

- **If the payload carries a version token:** the client requests the 24.0.1
  generation. Since URLs are hash addressed, expect either successful downloads
  of newer bundles, or `404`-style download failures surfaced through
  `AssetBundleDownloadBanner`, plus validation complaints such as
  `Validation. {0} not existed in download path {1}`.
- **If newer bundles do download:** 23.1.3 code loading 24.0.1 assets is
  undefined behaviour by design. Expect missing prefabs, shader/serialization
  mismatches, empty item icons, or a hard crash in `SceneDownloadAssetBundles`.
  This is the point of the experiment.
- **If the payload has no version token:** the log says so explicitly, and the
  client keeps downloading its own resources. Next step is then step 2 below.

## Reading the log

```
adb logcat -s OPG3D | grep 23.1.3-res-2401
```

Key lines:

- `armed - enabled=1 payload-rewrite=1 ...` - the module is live.
- `the client asks for bundle ConfigId 153 ...`
- `bundle config payload: N chars, ascii=1, head: ...` - the payload format.
- `payload retargeted: N '23.1.3'->'24.0.1' swap(s) ...` or
  `payload carries no '23.1.3' token ...`
- `bundle '<name>' hash '<hash>' -> https://...` - one line per composed URL
  (first 24, then every 32nd).
- `Downloader queue armed with N entry(ies) ...`

## Step 2: forcing real 24.0.1 entries

Because bundles are hash addressed, the only fully reliable way to fetch the
24.0.1 generation is to name it. Collect `bundle '<name>' hash '<hash>'` lines
from a 24.0.1 client (or from its config), then in
`res_2401_experiment_2313.cpp`:

```cpp
constexpr bool kAppendExtraEntries = true;
constexpr const char* kExtraEntries[] = {
    "some_bundle_name_0123456789abcdef0123456789abcdef",
    nullptr,
};
```

The separator defaults to a newline; adjust `kExtraSeparator` to whatever the
logged payload actually uses.

## Turning it off

Set `kEnabled = false` to keep the tracing and stop retargeting, or delete
`res_2401_experiment_2313.cpp` and `res_2401_experiment_2313.h` - nothing else
in the tree refers to them.
