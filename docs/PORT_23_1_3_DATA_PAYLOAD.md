# 23.1.3 (ARM64) in-APK asset payload (`assets/data/data.zip`)

## Problem

The optional resources (extra maps and their asset bundles) are fetched at
runtime from a backend that may disappear. If it does, the version becomes
unusable for missing resources alone. The resources have already been pulled
from a live device, so the APK should be able to carry them itself — the same
idea as the OBB provisioning in `OBB_PROVISIONING.md`, but for the resource
directory instead of the OBB.

## How to use it

Put the archive inside the APK at:

```
assets/data/data.zip
```

That is the only requirement. Any other `assets/data/*.zip` is accepted as a
fallback (the largest one wins), which keeps a name such as
`assets/data/data-android.zip` working.

The archive may be **stored or deflated**. A stored payload is parsed straight
out of the APK with no staging at all, so it costs no extra disk space; a
deflated payload is staged next to the resource root first and the staging file
is removed afterwards.

### Layout inside the archive

All three shapes work, and the choice is logged:

| Archive top level | Where it is unpacked |
| --- | --- |
| the bundle files themselves | the resource root |
| `<resource-dir>/…` | one level above, so the directory lands as-is |
| `<bundles-dir>/<resource-dir>/…` | two levels above (`persistentDataPath`) |
| `data/…`, `files/…`, `assets/…`, `resources/…` wrapping everything | the wrapper is stripped |

The last row exists because zipping a folder instead of its contents is the
easiest mistake to make.

## Where the game reads

`PGCompany.AssetBundles_v3.丄七丁丅丐与丁丗与` builds the resource root as

```
Path.Combine(Application.persistentDataPath, <litA>, <litB>)
```

in `专丌上丕丌丑丈丞世()` (RVA `0x1D32BBC`, `public static string`, no arguments),
and every consumer goes through that getter: the bundle enumerator
`丆与丏丙丐且丈丑万.下丈丒丁丈丏丅丈丈()` (`0x1D36CB0`), the per-entry loader
`丞丐一丄业丙且丛丞(string)` (`0x1D3773C`), the path helper `丕丕丑一丂三丝不与()`
(`0x1D32CE0`) and the bundle reader `丏丄丛丟丏丛上专丁`.

So the module **hooks that getter** rather than guessing the directory:

1. the hook calls the original and gets the real root string;
2. on the first call only, it provisions the payload into that root;
3. it returns the stock string unchanged.

The two obfuscated path components therefore never have to be resolved (they
are per-callsite metadata-usage literals that could not be named from the dump),
the destination is exactly what the game is about to read, and the timing is
correct because the getter runs before the bundles are enumerated.

The provisioning path deliberately makes **no managed calls** — only `read`,
`write`, `rename`, `stat`, `statvfs` and zlib — so it is safe no matter which
thread calls the getter. Reading the root string only walks its UTF-16 chars.

## Tolerating an unknown archive

The archive is assembled by hand, so its exact shape cannot be assumed:

* **archiver metadata is ignored**: directory entries, `__MACOSX/`, AppleDouble
  `._*` files, `.DS_Store`, `Thumbs.db`, `desktop.ini`, `.directory`, plus the
  archive comment and every extra field except zip64;
* **unsafe names are refused**, not written somewhere unexpected: absolute
  paths, `..`, backslash separators, drive-letter prefixes, over-long names;
* **zip64** central directories and entries are supported;
* **only stored and deflated** entries are accepted; anything else is reported
  per entry instead of being written half-decoded;
* every file is extracted to `<name>.opg3d-part`, checked against the CRC-32
  recorded in the archive and only then `rename`d into place, so an interrupted
  first launch cannot leave a truncated bundle behind;
* a file already present with exactly the recorded size is left alone;
* free space is checked up front with a 64 MiB margin;
* **nothing is ever deleted**, and every failure path logs the reason and
  leaves the resource root as it was.

## Idempotence

`<resource-root>/.opg3d-data.stamp` records the payload's CRC-32 and size and is
checked before anything is staged or read, so later launches short-circuit
immediately. Replacing `data.zip` changes the CRC and re-runs the provisioning.
The stamp is only written when no entry failed, so a partial run is retried on
the next launch.

## Logs

Prefix `23.1.3-assets-data:`.

```
23.1.3-assets-data: armed: 'assets/data/data.zip' inside the APK is unpacked into the game's own resource root on first use
23.1.3-assets-data: payload 'assets/data/data.zip' (612 MiB, crc 9a71f0c3), resource root '/data/user/0/com.pixel.gun3d/files/assetBundles-v2/android'
23.1.3-assets-data: 1483 files (611 MiB) -> '/data/user/0/com.pixel.gun3d/files' (archive contains both resource directories)
23.1.3-assets-data: unpacked 64 MiB in 137 files
23.1.3-assets-data: provisioning complete (written=1483 already present=0 ignored=4, 611 MiB unpacked)
```

Later launches print `this payload is already provisioned; skipping`, and an APK
without a payload prints `this APK carries no 'assets/data/data.zip' payload;
nothing to provision (the game keeps using its own downloader)`.

## Known limits

* The first launch extracts synchronously on whichever thread first asks for the
  resource root, during the loading scene. There are no input events then, so an
  ANR is unlikely, but a very large payload will visibly extend that first load.
  Every later launch short-circuits on the stamp.
* If the game asked for the root *before* the port finished installing its hooks,
  provisioning happens on the next call of the getter instead (it is called
  repeatedly, including by the per-entry path helper).
* A payload with entries whose names exceed the 320-byte ZIP name cap shared with
  `obb_provisioner.h` is skipped and reported.

## Files

* `opg3d/src/main/cpp/assets_data_2313.h` — the whole subsystem; the ZIP
  plumbing is shared with `obb_provisioner.h`, extended with a base offset so a
  stored payload can be parsed in place inside the APK.
* `opg3d/src/main/cpp/main.cpp` — `assets_data_2313::install_hooks(base)` in the
  init thread, reported as `assets-data=…` in the diagnostic line.
