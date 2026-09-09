# Gate C4: native Quest wand and locomotion build

## Scope and claim boundary

Gate C4 keeps the accepted C3 textured Hogwarts path and adds the first native
standalone gameplay-control layer. It was built, audited, installed, launched,
and accepted on the connected Quest 3. The user confirmed the visible wand,
left-stick locomotion, and right-stick snap turns in the textured Hogwarts
scene.

The APK contains no proprietary game assets. The wand geometry is decoded at
runtime from the user's external system/HPBase.u.

## Implemented slice

- One OpenXR action set binds Oculus Touch right grip pose, right aim pose,
  right analog trigger, left thumbstick, and right thumbstick.
- The visible wand root follows right grip position while the authoritative
  runtime aim orientation drives the prop axis. No competing aim guide is
  drawn.
- HPBase.WandMesh export 1092 is loaded through the existing geometry-only
  C ABI, normalized to 0.34 m, and rendered with the accepted procedural brown
  gradient because its original non-P8 material is not decoded yet.
- Left-stick movement is head-relative at 1.8 m/s with a 0.18 radial deadzone.
- Right-stick turn is a 30-degree snap with 0.72 engage and 0.35 release
  thresholds. The current virtual head position is preserved across the snap.
- Wand, eyes, and locomotion share one world transform. A pending LOCAL
  reference-space change resets accumulated locomotion.
- Trigger state is sampled and logged but gesture capture/spell dispatch is not
  part of C4.
- BSP collision, NPC population, animation, scripts, audio, UI, and saved-game
  progression remain outside this gate.

## Verification

Windows Release build and tests:

~~~text
hpvr_wand_tests             PASS
hpvr_hp1_gesture_tests      PASS
hpvr_quest_lifecycle_tests  PASS
hpvr_quest_view_tests       PASS
100% tests passed, 0 tests failed out of 4
~~~

The locomotion regression covers 1.8 m/s forward movement, snap-turn head-pivot
preservation, and held-stick latching.

Android NDK r27c compilation completed for arm64-v8a, including both new
wand shaders and the OpenXR input path:

~~~text
Generating generated/wand_frag.spv.h
Generating generated/wand_vert.spv.h
Building CXX object ... quest_scene.cpp.o
Building CXX object ... xr_vulkan_smoke.cpp.o
Linking CXX shared library libhpvr_quest.so
~~~

The normal offline Gradle invocation stopped before native packaging because
its cached generated R.jar was unreadable to Gradle state tracking. This was
not a C++ or shader failure. PACKAGE-QUEST-DEBUG.ps1 now performs the bounded
replacement of only libhpvr_quest.so in the previously valid debug container,
stores resources/native libraries uncompressed, zip-aligns, and debug-signs
the result.

Final audited package:

~~~text
path   = android/app/build/outputs/apk/debug/app-debug.apk
sha256 = 86003F2B5B5796815ADB751CC514CA4DC10154B4B022FC0AD271E2E3928EFD82
ABI    = arm64-v8a
signature = APK Signature Scheme v3
alignment = valid
resources.arsc = stored
proprietary assets = 0
Touch action symbols = linked
WandMesh loader = linked
~~~

## Quest 3 runtime acceptance

The audited APK above was installed on Quest 3 serial `2G0YC1ZF760BPC` as
`io.github.hpvr.quest/android.app.NativeActivity`. After the headset and Touch
controllers were awake, device logs reported:

~~~text
gate=C4
scene content=HOGWARTS vertices=60012 texture_layers=88
input status=READY profile=oculus_touch
wand=TRACKED
move_frames > 0
snap_turns > 0
~~~

The user then separately confirmed all three acceptance items: the wand is
drawn, the left stick moves, and the right stick turns. This establishes C4
runtime acceptance, not merely APK installation. Flipendo capture and dispatch
remain outside C4.
