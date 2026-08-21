# OBB self-provisioning (armory v11)

The expansion file travels inside the APK and the game unpacks it to
`/storage/emulated/0/Android/obb/<package>/main.<versionCode>.<package>.obb`
itself, on the first launch, before Unity starts. The end user installs one
file and nothing else.

Implementation: `opg3d/src/main/cpp/obb_provisioner.h`, called from the ELF
constructor in `main.cpp`.

## How to repack

Put the payload into the APK's assets:

```
assets/obb/main.1526.com.pixel.gun3d.obb     <- preferred
assets/obb/anything.obb                      <- also accepted
assets/anything.obb                          <- accepted, lower priority
assets/obb/patch.<anything>.obb              <- optional patch payload
```

The asset name does not have to be correct: the destination name is always
rebuilt from the APK's own `package` and `versionCode`, so a repack for a
different version code needs no source change here. Selection order is
`assets/obb/main*` > `assets/obb/*` > `assets/*`, and a basename starting with
`patch` is treated as the patch payload instead of the main one.

Store the asset uncompressed if the packer allows it (`aapt add -0 .obb`, or
`androidResources { noCompress += 'obb' }`). An `.obb` is already a ZIP, so
compressing it only makes the APK bigger and the first launch slower. A
deflated asset still works — the module inflates it on the fly.

No manifest change and no extra permission is required: `Android/obb/<own
package>` is app-specific storage, writable without `WRITE_EXTERNAL_STORAGE`
and still reachable by path under scoped storage.

## Why the ELF constructor, and why no JNI

Unity decides whether its data exists while `libunity`/`libil2cpp` initialise.
Anything later is too late, so the work cannot go on the phase-0 thread that
waits for `libil2cpp.so` — by then the engine has already looked for the file.
The constructor of this library runs while the APK's native libraries are
still being loaded, which is the last point where the expansion file can still
appear "before" the engine.

That rules out the Java APIs: there is no guarantee that `JNI_OnLoad` runs for
us at all (a library pulled in as a `DT_NEEDED` dependency only gets its
constructors), and calling into the VM from a linker constructor is not safe in
general. So the module uses no JNI, no `Context`, no `PackageManager`, no
`AssetManager`, and no IL2CPP:

| needed | official API | what this module does instead |
| --- | --- | --- |
| own APK | `Context.getPackageCodePath()` | first `*.apk` mapping in `/proc/self/maps`, `base.apk` preferred |
| package name | `Context.getPackageName()` | `package` attribute of the binary `AndroidManifest.xml`, cross-checked against `/proc/self/cmdline` |
| version code | `PackageInfo.versionCode` | `versionCode` attribute of the same manifest |
| payload | `AssetManager.open()` | ZIP central directory of the APK (zip64 aware), `pread64` + raw inflate |
| destination | `Context.getObbDir()` | `$EXTERNAL_STORAGE/Android/obb/<package>`, falling back to `/storage/emulated/0` then `/sdcard` |

`JNI_OnLoad` still calls `provision()` as defence in depth; the call is
idempotent, so on the normal path it returns immediately.

## Guarantees

* **Idempotent.** A destination file whose size already matches the payload is
  left completely alone; that is the only work done on every later launch.
* **Atomic.** The copy goes to `<dest>.opg3d-part` and is renamed only after
  the size and the CRC-32 recorded in the APK both match and `fsync` succeeded.
  A killed first launch cannot leave a half-written expansion file behind.
* **Non-destructive.** Files belonging to other version codes are kept, and
  every failure path leaves storage exactly as it was.
* **Fail-open for the rest of the library.** Provisioning failure only logs;
  the hook modules install normally.
* **Permissions.** Directories are created 0755 and the file 0644 (`chmod` is a
  no-op on FUSE emulated storage, where the mode is fixed by the volume, and
  `EPERM` from it is therefore ignored).
* **Space.** The volume is checked for the payload size plus a 32 MiB margin
  before anything is written.

The cost is that the loading thread blocks for one sequential copy on the first
launch. That is deliberate: it is the only way the file can be in place before
the engine looks for it.

## Log lines (`adb logcat -s OPG3D`)

First launch:

```
obb: apk='/data/app/~~.../base.apk' (1342177280 bytes)
obb: package='com.pixel.gun3d' versionCode=1526 expected='main.1526.com.pixel.gun3d.obb'
obb: extracting 'assets/obb/main.1526.com.pixel.gun3d.obb' (536870912 bytes, stored) to '/storage/emulated/0/Android/obb/com.pixel.gun3d/main.1526.com.pixel.gun3d.obb'
obb: copying 'main.1526.com.pixel.gun3d.obb': 67108864/536870912 bytes (12%)
obb: '/storage/emulated/0/Android/obb/com.pixel.gun3d/main.1526.com.pixel.gun3d.obb' ready (536870912 bytes, mode 0644, 9123 ms)
obb: provisioning finished in 9130 ms; Unity has not started yet
init: libopg3d build 13.2.1 obb self-provisioning (armory v11) built ...
```

Every later launch is one line:

```
obb: '/storage/emulated/0/Android/obb/com.pixel.gun3d/main.1526.com.pixel.gun3d.obb' is already in place (536870912 bytes); nothing to copy
```

## Failure modes and what they mean

| line | meaning |
| --- | --- |
| `obb: this APK carries no assets/obb payload` | plain build without an embedded expansion file; the game uses whatever is on storage |
| `obb: own APK not found in /proc/self/maps` | the library was loaded from outside the APK; provisioning is skipped |
| `obb: AndroidManifest.xml could not be parsed` | falls back to `/proc/self/cmdline` for the package and to the asset name for the version code |
| `obb: version code unknown` | neither the manifest nor the asset name gave one; the file cannot be named, nothing is written |
| `obb: no writable Android/obb/<package> directory` | storage denied or not mounted; nothing is written |
| `obb: not enough free space` | payload + 32 MiB does not fit; nothing is written |
| `obb: CRC-32 mismatch` / `produced N bytes, the APK says M` | corrupted repack or truncated read; the temporary file is deleted and the old state kept |
| `obb: ... does not start with a ZIP signature` | the copied asset is not an expansion file; the game will likely reject it |
