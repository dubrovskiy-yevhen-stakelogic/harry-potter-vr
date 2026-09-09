# Gate C2: Quest owned-data import and package probe boundary

## Scope and claim boundary

Gate C2 prepares the first external retail-data step without putting any game
asset in the repository or APK. The importer was exercised in its default
read-only plan mode only. Nothing was copied to the headset, the APK was not
installed or launched, and native package parsing on Quest remains a runtime
claim to be tested later.

## Import contract

`IMPORT-QUEST-DATA.ps1` reads a user-selected, legally owned HP installation.
Its default is a non-mutating inventory. Actual transfer requires the explicit
`-Copy` switch and an authorized adb device.

The importer preserves relative paths under the app-specific external root:

```text
/sdcard/Android/data/io.github.hpvr.quest/files/HP
```

It includes runtime content below `Maps`, `Music`, `Sounds`, `system`, and
`Textures`, while excluding Windows executables and DLLs, drivers, logs,
uninstall data, help/support files, and saves. It never stages those files in
the repository. Existing unrelated device files are not deleted. `adb push
--sync` performs the transfer, after which SHA-256 is compared for the three
required sentinels:

```text
system/HPBase.u
system/HarryPotter.u
Maps/Lev_Tut1.unr
```

The current golden installation produced this dry-run plan:

```text
files = 286
bytes = 399,910,217
MiB   = 381.38
action = NONE
```

## Native ARM64 data probe

When all three sentinels are present, the Quest host now calls the already
tested portable `hpvr_hp1_load_player_start_utf8` decoder directly against the
external `Lev_Tut1.unr`. It uses the PCVR-accepted `0.02` metres-per-Unreal-unit
scale and requests PlayerStart ordinal zero. The data remains read-only.

Success requires status OK, PlayerStart ABI 1, a non-empty PlayerStart list,
and serialized location data. The host logs the selected object, position,
yaw, count, and scale. Failure is nonfatal for the C1 stereo diagnostic and is
reported with both return/report status and the contained decoder error.

This deliberately proves only that the portable package parser is present in
the ARM64 binary and provides a precise future runtime checkpoint. It does not
yet upload BSP vertices or textures to Vulkan, render Hogwarts, create NPCs,
or execute UnrealScript.

## Offline evidence

The importer dry run completed with `action=NONE`. The updated native APK then
built successfully and `VERIFY-QUEST-APK.ps1` found the required
`hpvr_hp1_load_player_start_utf8` symbol in the AArch64 library in addition to
the C1 frame-loop symbols.

```text
path   = android/app/build/outputs/apk/debug/app-debug.apk
size   = 1,139,244 bytes
sha256 = F7CE39772F6F4B8CEA5B4AFD224646D05E415C0949F8F23F8C6B9D5B6226DC05
package audit = PASS
proprietary assets in APK = 0
```

This hash identifies the C2 build produced before any device action. A later
source change or rebuild is expected to produce another debug-APK hash.

## Next implementation boundary

After the native C1 session and C2 external parser are accepted on Quest, the
next renderer slice should load the existing bounded textured BSP scene once,
upload its 60,009 vertices and 88-layer 256x256 RGBA8 texture array to Vulkan,
and apply the same PlayerStart transform already accepted in PCVR. Collision,
NPCs, wand input, and spells should remain separate additions so failures stay
attributable.

## 2026-09-04 device status update

The C2-compatible APK was installed and its retained red-left/blue-right
fallback was visually accepted. External HP data was not transferred, so the
native PlayerStart probe remains linked/offline evidence rather than a
device-runtime parsing claim.
