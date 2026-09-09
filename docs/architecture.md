# Runtime architecture

The demo is a native C++20 Android ARM64 application using OpenXR, Vulkan and
AAudio. It loads data from the US PC edition of *Harry Potter and the Sorcerer's
Stone* (2001). It does not run `HP.exe` or include the original engine.

## Components

| Location | Responsibility |
| --- | --- |
| `src/wand/` | UE1 package readers, dependency linking, BSP geometry, textures, skeletal meshes, animation and sound extraction; gesture projection and scoring. |
| `src/quest/` | Portable locomotion, gestures, spell targets, lighting, menus, tutorial progress, saves and VR settings. |
| `android/app/src/main/cpp/android_main.cpp` | Android lifecycle, data-root checks and OpenXR startup. |
| `android/app/src/main/cpp/xr_vulkan_smoke.cpp` | OpenXR session, controller input, stereo rendering, tracking and frame timing. Despite the historical filename, this is the current XR backend. |
| `android/app/src/main/cpp/quest_scene.cpp` | Scene loading, Vulkan draws, collision, characters, cutscene commands and tutorial events. |
| `android/app/src/main/cpp/quest_audio.cpp` | Music, dialogue and effects mixed through AAudio. |
| `android/app/src/main/cpp/quest_reflections.cpp` | Color/depth history for SSR, sampled by the material shader; no extra scene render. |
| `tools/release/` | Windows player installer and owned-data preparation. |

The scene runtime interprets the cutscene command tracks stored in the map and
implements the tutorial's gameplay in C++. It is not a general UnrealScript VM.
The playable scope is `Lev_Tut1`, ending after the first Flipendo lesson.

## Data and storage

The player installer resolves the map's package dependencies and prepares
fingerprint-named PCM from the user's audio. It leaves the PC installation
unchanged and imports the result to:

```text
/sdcard/Android/data/io.github.hpvr.quest/files/HP/
  Maps/  Music/  Sounds/  system/  Textures/
  Cache/Audio/*.s16
```

The APK contains code, shaders, app resources and third-party libraries, not game
assets. Scene loading reads the external packages; audio uses the prepared cache.
Save slots are in the app's private `files/SaveGames/` directory. VR preferences
use private `files/vr-settings.0` and `.1` banks. Both formats use generations and
checksums. Original PC saves are not imported.

## Rendering and input

OpenXR supplies head/controller poses and predicted display timestamps. The
runtime combines tracking with locomotion or the selected cinematic camera.
Cutscenes support Harry's first-person view and a theatrical view with 6DOF head
movement. The world-space VR menu is available during gameplay and cutscenes.

Vulkan renders each eye. Optional SSR reads shared left-eye history and excludes
the HUD from capture. Render scale and SSR strength are saved VR settings.
Gesture coordinates and input behavior are documented in
[wand-gesture-contract.md](wand-gesture-contract.md).

## Build

The root CMake project builds portable libraries, probes and tests on Windows.
The Android Gradle project builds the OpenXR application with the NDK. See
[RELEASE-BUILD.md](RELEASE-BUILD.md) for packaging/signing and
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for dependencies.
