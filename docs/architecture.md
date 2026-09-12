# Runtime architecture

The port is a native C++20 Android ARM64 application using OpenXR, Vulkan and
AAudio. It loads data from the US PC edition of *Harry Potter and the Sorcerer's
Stone* (2001). It does not run `HP.exe` or include the original engine.

## Components

| Location | Responsibility |
| --- | --- |
| `src/wand/` | UE1 package readers, dependency linking, BSP, textures, meshes, animation and sound extraction; gesture projection and scoring |
| `src/quest/` | Portable locomotion, gestures, spell targets, lighting, menus, progress, saves and VR settings |
| `android/app/src/main/cpp/android_main.cpp` | Android lifecycle, data-root checks and OpenXR startup |
| `android/app/src/main/cpp/xr_vulkan_smoke.cpp` | XR backend: session, controllers, stereo rendering, tracking and frame timing |
| `android/app/src/main/cpp/quest_scene.cpp` | Scene loading, Vulkan draws, collision, characters, cutscenes and gameplay events |
| `android/app/src/main/cpp/quest_audio.cpp` | AAudio music, dialogue and effects mixer |
| `android/app/src/main/cpp/quest_reflections.cpp` | Shared color/depth history for SSR |
| `android/app/src/main/cpp/quest_voice_cast_android.cpp` | Gated microphone capture and offline voice worker |
| `tools/release/` | Windows installer and owned-data preparation |

The runtime interprets supported cutscene command tracks and implements gameplay
in C++. It is not a general UnrealScript VM. The playable scope is `Lev_Tut1`
and `Lev_Tut1b`, ending after the Flipendo Challenge.

## Data and storage

The installer resolves both maps' package dependencies and prepares scene caches
and fingerprint-named PCM from owned audio. The PC installation stays unchanged.
Imported data is stored under:

```text
/sdcard/Android/data/io.github.hpvr.quest/files/HP/
  Maps/  Music/  Sounds/  system/  Textures/
  Cache/Audio/*.s16
  Cache/Scenes/*.hpvc
```

The APK contains code, shaders, app resources, licensed voice-model weights and
third-party libraries, but no original game assets. Scene caches are validated
before adoption; missing or rejected caches use runtime preparation.

Save slots live in the app-private `files/SaveGames/` directory. VR preferences
use `files/vr-settings.0` and `.1`. Both formats use generations and checksums.
Original PC saves are not imported. Challenge recovery uses original save-book
checkpoints or the level entrance.

## Rendering and input

OpenXR provides head/controller poses and predicted display times. Tracking is
combined with locomotion or the selected cinematic camera. Both first-person and
theatrical cutscenes allow 6DOF head movement. The live VR menu works in gameplay
and cutscenes. Recenter adjusts the tracking reference while preserving the
player's body position in the world.

Vulkan renders each eye. SSR samples shared left-eye history without rendering
the scene again; HUD/wand overlays are excluded from that history. Abyss height
fog is evaluated in the world material pass. Flame, glow and pickup effects use
bounded batches.

Gesture projection and scoring are described in
[wand-gesture-contract.md](wand-gesture-contract.md). Voice input is restricted
to supported gameplay and valid target generations; unarmed input is discarded.
Recognition runs on a CPU worker, not on the render thread or GPU. Public builds
do not record or upload microphone audio.

## Build

The root CMake project builds portable libraries, probes and tests. Android
Gradle builds the application with the NDK. See [release builds](RELEASE-BUILD.md),
[scene preparation](LOADING-AND-PICKUPS.md), [voice dependencies](../tools/voice/README.md)
and [third-party notices](THIRD-PARTY-NOTICES.md).
