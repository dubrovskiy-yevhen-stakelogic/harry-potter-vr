# Gate C3: native Quest textured Hogwarts build

## Scope and claim boundary

Gate C3 replaces the accepted red/blue diagnostic only when a complete,
read-only external HP data root is available. It builds the first native Quest
render of the tutorial Hogwarts map. The first C3 APK was built, audited, and
installed on Quest 3, and the selected game data was copied and verified. Its
first run exposed a scene-link failure described below. A second, corrected
APK was built, audited, installed, and run. Runtime logs prove the textured
Hogwarts path reached GPU upload and submitted 600 frames, and the user
confirmed that Hogwarts was visible. This is visual/content-path acceptance,
not a performance or thermal acceptance claim.

The APK contains no proprietary package, texture, executable, music, or speech
asset. All scene content remains under the user's app-specific external data
root and is decoded at runtime.

## Implemented renderer slice

The Android host now:

- loads `Maps/Lev_Tut1.unr` through the existing clean-room package linker;
- requires PlayerStart ordinal zero and its serialized location/yaw;
- applies the PCVR-accepted `0.02` metres-per-Unreal-unit scale and direct
  PlayerStart transform;
- converts the full bounded BSP stream to one immutable Vulkan vertex buffer;
- uploads an 88-layer `256x256` `VK_FORMAT_R8G8B8A8_SRGB` texture array;
- checks image-dimension, array-layer, sampled-image, and transfer-destination
  capabilities before allocating the texture;
- compiles and embeds GLSL shaders with the pinned NDK `glslc`;
- samples layer-indexed material UVs and applies masked-material alpha discard;
- creates a two-layer depth image for every OpenXR swapchain image;
- applies each runtime eye pose and asymmetric FOV through a tested Vulkan
  zero-to-one projection matrix;
- records one depth-tested map draw per eye;
- retains the red-left/blue-right C1 clear whenever external data is absent or
  rejected.

This gate does not include NPCs, skeletal animation, Touch actions, a visible
wand, gestures, spells, locomotion, collision, UnrealScript execution, audio,
translucent material sorting, or gameplay state.

## Read-only game-data result

The exact C3 parser inputs were exercised against the golden installation:

```text
data root             = C:\Program Files\HP
map                   = Maps\Lev_Tut1.unr
metres per Unreal unit = 0.02
triangle limit        = 100000
available triangles   = 20003
selected triangles    = 20003
omitted triangles     = 0
vertices              = 60009
texture layers        = 88
layer size            = 256x256
RGBA bytes            = 23068672
RGBA FNV-1a64         = 49a07fdd85b40ab6
decoded textures      = 87
fallback materials    = 1
fallback triangles    = 11
```

The single fallback is the already identified dynamic `FireTexture`; C3 does
not falsely report it as a decoded static P8 texture.

## Tests and ARM64 build

The Windows regression suite passed:

```text
hpvr_wand_tests             PASS
hpvr_hp1_gesture_tests      PASS
hpvr_quest_lifecycle_tests  PASS
hpvr_quest_view_tests       PASS
4/4 tests passed
```

The new view test checks Vulkan depth range, eye-height and position inversion,
quaternion normalization, and rejection of invalid poses/FOV/depth ranges.

The native Android CMake build completed with NDK r27c, API 32, Clang 18.0.3,
ARM64, `-Wall -Wextra -Wpedantic -Werror`, and the packaged Khronos OpenXR
loader. The resulting shared object is ELF64 AArch64 and dynamically requires
the expected OpenXR, Vulkan, Android, log, math, dynamic-loader, and C runtime
libraries.

The normal Gradle wrapper could not reuse its user-profile cache in this Codex
turn because the managed filesystem denied writes below
`C:\Users\user\.gradle`. The native library was therefore compiled directly
with the same pinned NDK/CMake configuration and inserted into the unchanged,
previously installed C2 APK container before zip alignment and debug signing.
This is a local packaging workaround, not a claim that the normal Gradle task
ran successfully in this turn.

## APK audit

```text
path   = android/app/build/outputs/apk/debug/app-debug.apk
size   = 995,989 bytes
sha256 = C8EA3E4A85261E3A2AD74D510B51C03AB1F5EA7EE63EE6F030B7EBC9AF3AEBCD
```

`VERIFY-QUEST-APK.ps1` now extracts and inspects the actual ARM64 library from
inside the supplied APK. It confirmed AArch64 ELF identity, the required
OpenXR/Vulkan dependencies, frame-loop symbols, PlayerStart probe, graphics
pipeline creation, buffer-to-image upload, and draw submission. The final
summary was:

```text
status=PASS
abi=arm64-v8a
proprietary_assets=0
resources_arsc=STORED
openxr_loader=PACKAGED
stereo_frame_loop=LINKED
player_start_probe=LINKED
hogwarts_pipeline=LINKED
signature=VALID
alignment=VALID
```

The APK is signed with debug APK Signature Scheme v3. An initial manually
rebuilt container compressed `resources.arsc`; Android rejected it with install
error `-124` before replacing the installed app. The corrected package stores
that entry uncompressed, remains zip-aligned, passes the expanded audit, and
then installed successfully. `VERIFY-QUEST-APK.ps1` now rejects this packaging
error explicitly.

## Device preparation result

The corrected package was installed on connected Quest 3
`2G0YC1ZF760BPC` with `adb install -r`. Read-only package inspection reported:

```text
package         = io.github.hpvr.quest
versionCode     = 1
versionName     = 0.1.0-dev
primaryCpuAbi   = arm64-v8a
minSdk          = 32
targetSdk       = 35
signing version = 3
install result  = Success
```

`IMPORT-QUEST-DATA.ps1 -Copy` then transferred the approved content into:

```text
/sdcard/Android/data/io.github.hpvr.quest/files/HP
files = 286
bytes = 399910217
device du = 382M
```

The importer compared local and remote SHA-256 values and reported `MATCH` for
all required sentinels:

```text
system/HPBase.u       30b5ef44e9755aa9c020be9d863e35335c26c2d6988fd0a00a347a98c44e105d
system/HarryPotter.u  5f18066ac7d6a64ba315a19753308613c0819b3944da551a17bd0f710560cf60
Maps/Lev_Tut1.unr     c3a23b396e67fb43ed6e018944a60496b19ee8a15d5253ebf26029abaca150f7
```

The golden PC installation remained read-only. This transfer establishes data
presence and integrity only; it does not prove that the ARM64 parser or Vulkan
renderer consumed the data.

## First launch result and corrective build

The first installed C3 package was launched with a fresh log capture. It proved
that the imported files were visible and that the ARM64 PlayerStart parser
worked:

```text
required_present=3 required_total=3 status=READY
player_start=PlayerStart0 ordinal=0 available=1 scale=0.02000
```

The full scene then rejected `Core.Function`. The Windows data root includes
`Core.dll`, so the linker knew missing `Core.u` exports could be native. The
Quest import correctly excluded all Windows DLLs, but that removed the previous
native-companion signal. C3 consequently fell back to the red/blue clear and
submitted 900 diagnostic frames. The user correctly rejected that result.

The graph now retains a bounded registry of the twelve native module identities
observed in this HP1 release while continuing to exclude their Windows binary
payloads. Arbitrary unknown missing packages still fail closed. New regression
tests cover a data/native `Core` package and a DLL-only `Window` requirement
with no DLL present.

An exact temporary mirror of the import contract was then built and checked:

```text
mirror files = 286
mirror DLLs  = 0
textured BSP status = ok
triangles = 20003
vertices = 60009
layers = 88
RGBA FNV-1a64 = 49a07fdd85b40ab6
```

C3 startup is now fail closed: missing/rejected data or scene linking logs
`C3_SCENE_REQUIRED` and exits instead of presenting the red/blue C1 diagnostic
as apparent C3 progress.

The corrected ARM64 APK passed the full embedded-library audit:

```text
path   = android/app/build/outputs/apk/debug/app-debug.apk
size   = 995,989 bytes
sha256 = 6C3DED585A3746AFC3E1CF338DA68A9ACDB75B29971AD16385DFC806E05DC3EC
```

The corrected package was installed and run under fresh log capture. The
required pre-visual markers were present:

~~~text
[hpvr.quest.scene.gpu] status=READY vertices=60012 vertex_bytes=1680336 texture_bytes=23068672 layers=88
[hpvr.quest.session] status=CREATED width=1680 height=1760 images=3 array_layers=2 format=43 depth_format=126 scene=HOGWARTS
[hpvr.quest.frame] status=SUBMITTED count=1 views=2 content=HOGWARTS vertices=60012 texture_layers=88
[hpvr.quest.frame] status=SUBMITTED count=300 views=2 content=HOGWARTS vertices=60012 texture_layers=88
[hpvr.quest.frame] status=SUBMITTED count=600 views=2 content=HOGWARTS vertices=60012 texture_layers=88
~~~

The device GPU identified as Adreno 740. After the user exited, the session and
OpenXR instance were destroyed cleanly with no recorded native crash. The user
then reported that Hogwarts was visible.

The exact 286-file/no-DLL Windows mirror produced 60,009 vertices, while this
device run produced 60,012. That one-triangle difference does not invalidate
the visual acceptance, but exact host/device scene determinism remains open and
must not be claimed yet.
