# Gate C1: native Quest OpenXR/Vulkan stereo smoke build

## Scope and claim boundary

Gate C1 advances the Android ARM64 host from instance discovery to a complete
native stereo submission path. This is build and static-package evidence only:
the APK was deliberately not installed or launched, so there is no current
Quest runtime or visual acceptance claim.

The standalone diagnostic renders a solid red left eye and solid blue right
eye. It does not yet load Hogwarts, game textures, characters, retail scripts,
controller actions, or the PCVR renderer.

## Implemented native frame path

`XrVulkanSmoke` now owns the graphics/session boundary and:

- obtains the Vulkan requirements and creation functions through
  `XR_KHR_vulkan_enable2`;
- asks OpenXR to create Vulkan 1.1 instance/device objects and uses the
  runtime-selected physical device;
- chooses a graphics queue, binds it through `XrGraphicsBindingVulkan2KHR`,
  and creates an OpenXR session plus `LOCAL` reference space;
- validates exactly two primary-stereo views and non-zero recommended image
  dimensions;
- selects an advertised RGBA/BGRA SRGB or UNORM colour format;
- creates one two-layer OpenXR swapchain and rejects empty format/image lists;
- creates one Vulkan view/framebuffer per eye layer for every runtime-owned
  swapchain image;
- follows `xrWaitFrame -> xrBeginFrame -> xrLocateViews -> acquire/wait ->
  Vulkan submit/GPU completion -> release -> xrEndFrame`;
- submits one `XrCompositionLayerProjection` with each runtime eye pose/FOV;
- clears array layer 0 red and array layer 1 blue;
- handles `READY`, `STOPPING`, `EXITING`, instance loss, and session loss;
- tears down the session/swapchain on pause or window loss and recreates it on
  the next eligible Android lifecycle generation.

Every OpenXR/Vulkan failure is logged with the operation and numeric result.
The frame path releases an acquired image only after a successful wait and
stops on a submission error rather than recycling an unsignalled fence.

## Build and static audit

The pinned C0 toolchain was reused unchanged: JDK 21, Gradle 9.3.1, Android
Gradle Plugin 8.7.3, SDK 35, NDK 27.2.12479018, and Khronos Android OpenXR
loader 1.1.43. `BUILD-QUEST-DEBUG.ps1` completed successfully.

```text
path   = android/app/build/outputs/apk/debug/app-debug.apk
size   = 1,139,031 bytes
sha256 = DB64926CC62BFCED3325214B75119DEF8BD2D58BBE1C1CE97AFB89299D4A2944
```

`VERIFY-QUEST-APK.ps1` passed the existing package, ABI, manifest, signature,
alignment, dependency, and proprietary-asset checks. It now additionally
requires linked dynamic symbols for the frame loop:

```text
ANativeActivity_onCreate
android_main
xrWaitFrame
xrEndFrame
vkQueueSubmit
hpvr_wand_abi_version
hpvr_hp1_gesture_abi_version
```

The final audit summary was:

```text
status=PASS
abi=arm64-v8a
proprietary_assets=0
openxr_loader=PACKAGED
stereo_frame_loop=LINKED
signature=VALID
alignment=VALID
```

The Windows portable/lifecycle regression suite also remained green:

```text
hpvr_wand_tests             PASS
hpvr_hp1_gesture_tests      PASS
hpvr_quest_lifecycle_tests  PASS
3/3 tests passed
```

## Remaining runtime gate

An authorized Quest install and launch must still prove loader initialization,
Vulkan device creation, session state transitions, balanced stereo submission,
red-left/blue-right visual assignment, and pause/resume recreation on the
actual headset. Only after that narrow native baseline is accepted should the
external game-data importer and first Hogwarts render bridge become the active
standalone runtime path.

## 2026-09-04 device acceptance update

A later C2-compatible APK retaining the unchanged C1 red/blue renderer was
installed and launched on the connected Quest 3. The user reported red in the
left eye and blue in the right eye. This closes the visual stereo/order portion
of C1 on the device. It does not by itself prove pause/resume or window-loss
session recreation, because no current lifecycle log capture accompanied that
visual report.
