# Architecture

The native C++20 Android ARM64 application uses OpenXR, Vulkan and AAudio. It
loads data from the US PC edition of *Harry Potter and the Sorcerer's Stone*
(2001), without running the original executable or engine.

## Components

| Location | Responsibility |
| --- | --- |
| `src/wand/` | UE1 packages, dependencies, BSP, textures, models, animation and sound readers; gesture projection and scoring. |
| `src/quest/` | Movement, spells, lighting, menus, tutorial logic, saves and VR settings. |
| `android/app/src/main/cpp/android_main.cpp` | Android lifecycle, data checks and OpenXR startup. |
| `android/app/src/main/cpp/xr_vulkan_smoke.cpp` | XR session, controllers, stereo rendering, tracking and frame timing. |
| `android/app/src/main/cpp/quest_scene.cpp` | Scene loading and drawing, collision, characters, cutscene commands and events. |
| `android/app/src/main/cpp/quest_audio.cpp` | Music, dialogue and effects through AAudio. |
| `android/app/src/main/cpp/quest_reflections.cpp` | Color/depth history for SSR without another scene render. |

The scene executes a subset of the map's cutscene commands, with tutorial logic
implemented in C++. It is not a general UnrealScript VM. The demo uses `Lev_Tut1`
through the first Flipendo lesson.

## Data and storage

Original packages are stored outside the APK:

```text
/sdcard/Android/data/io.github.hpvr.quest/files/HP/
  Maps/  Music/  Sounds/  system/  Textures/
  Cache/Audio/*.s16
```

`PREPARE-QUEST-AUDIO.ps1` and `PREPARE-QUEST-FRONTEND.ps1` build the audio cache
from the user's copy. Saves are stored in the private `files/SaveGames/` directory;
VR settings use `files/vr-settings.0` and `.1`. These formats include generations
and checksums. Original PC saves are not imported.

## Rendering and input

OpenXR provides head/controller poses and frame timestamps. This source version
uses a theatrical cutscene camera with physical 6DOF. The wand is tracked
independently of the head.

Vulkan renders each eye. SSR uses left-eye history and excludes the HUD from
capture. Render scale and SSR strength are saved VR settings. Wand behavior is
documented in [wand-gesture-contract.md](wand-gesture-contract.md).

Build commands are in [SOURCE-KIT-README.md](../SOURCE-KIT-README.md).
