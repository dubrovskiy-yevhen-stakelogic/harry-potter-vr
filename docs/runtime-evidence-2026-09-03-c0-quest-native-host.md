# Gate C0: native Quest ARM64 host and package boundary

## Scope

Gate C0 begins the standalone path after the accepted PC Quest Link vertical
slice. It proves a clean-room Android packaging and host boundary only. The APK
was not installed or launched, no OpenXR call ran on the headset, and this gate
does not claim a Vulkan device, OpenXR session, swapchain, submitted stereo
frame, game scene, or standalone runtime acceptance.

## Original host and dependency boundary

The repository now contains an original Android NativeActivity host under
`android/app/src/main/cpp`. It consumes two separately identified platform
dependencies rather than copying a game or third-party engine source tree:

- `org.khronos.openxr:openxr_loader_for_android:1.1.43`, whose packaged
  `META-INF/LICENSE` is Apache License 2.0;
- NDK r27c `native_app_glue`, compiled directly from the installed NDK; its
  adjacent `NOTICE` is also Apache License 2.0.

No Meta sample source was copied. No proprietary HP1 package, executable,
texture, sound, or derived dump is present in the Android source tree or APK.

## Implemented host boundary

The ARM64 host now:

- enters through `android.app.NativeActivity` / `android_main`;
- records Android start, resume, pause, stop, window and focus transitions;
- turns resume/window eligibility into monotonic session generations so later
  OpenXR sessions can be torn down and recreated deterministically;
- calls `xrInitializeLoaderKHR` with the Java VM and Activity object;
- requires `XR_KHR_android_create_instance` and `XR_KHR_vulkan_enable2`;
- creates an Android-aware OpenXR instance, selects the HMD system, and rejects
  a primary stereo view configuration whose view count is not exactly two;
- links the same portable `hpvr_wand` library used on PC and checks wand ABI 1
  plus HP1 gesture ABI 1 at startup;
- reports the app-specific external `HP` root and checks three read-only
  sentinels (`HPBase.u`, `HarryPotter.u`, and `Lev_Tut1.unr`) without requiring
  broad storage permissions or embedding the files.

The runtime log milestones are explicit:

```text
[hpvr.quest.host] status=ENTER abi=arm64-v8a gate=C0
[hpvr.quest.portable_core] wand_abi=1 gesture_abi=1 status=READY
[hpvr.quest.data] ... bundled_assets=0 status=READY|IMPORT_REQUIRED
[hpvr.quest.openxr] status=INSTANCE_READY ... views=2 vulkan_enable2=1
```

They are code paths awaiting a later authorized headset run, not current device
evidence.

## Build and package evidence

The reproducible local build uses:

- JDK 21 from `C:\Dev\android-toolchain\jdk21`;
- cached Gradle 9.3.1 and Android Gradle Plugin 8.7.3;
- compile/target SDK 35, minimum SDK 32;
- NDK 27.2.12479018, `arm64-v8a` only;
- Khronos OpenXR loader AAR 1.1.43 from the existing Gradle cache.

`BUILD-QUEST-DEBUG.ps1` completed `:app:assembleDebug` successfully. The
resulting development package is intentionally ignored by Git:

```text
path   = android/app/build/outputs/apk/debug/app-debug.apk
size   = 1,129,601 bytes
sha256 = E582B92A3232F5AF69E02BCB711FB8C6ECCEE0E4A7D5EF128C32F84D578102AB
```

`VERIFY-QUEST-APK.ps1` then passed all static checks:

```text
package             = io.github.hpvr.quest
version              = 0.1.0-dev (1)
native ABI           = arm64-v8a only
libhpvr_quest.so     = ELF64 AArch64
OpenXR loader        = packaged
debug signature      = valid APK Signature Scheme v2
zip alignment        = valid
proprietary packages = 0
```

The unstripped ARM64 library exports the required NativeActivity and portable
ABI symbols: `ANativeActivity_onCreate`, `android_main`,
`hpvr_wand_abi_version`, and `hpvr_hp1_gesture_abi_version`. Its dynamic
dependencies include `libopenxr_loader.so`, `libvulkan.so`, `libandroid.so`,
and `liblog.so`; the C++ runtime is statically linked.

## Regression evidence

The new host-independent lifecycle test passes pause/resume, focus loss,
window loss/recreation, destroy, and monotonically increasing session
generation cases. The exact three C++ test targets were built sequentially to
avoid Visual Studio's unrelated parallel CMake-regeneration race, then all
passed:

```text
hpvr_wand_tests             PASS
hpvr_hp1_gesture_tests      PASS
hpvr_quest_lifecycle_tests  PASS
3/3 tests passed
```

## Next gate

Gate C1 must create Vulkan through `XR_KHR_vulkan_enable2`, create and drive an
OpenXR session, allocate the two-eye swapchain, submit a finite stereo clear or
triangle frame loop, and actually exercise pause/resume session recreation.
Only after an APK is installed and current Quest logs plus a visual check pass
can native runtime behavior be claimed.

The C1 source and APK were subsequently implemented and passed their offline
build/static audit. Device execution and visual acceptance remain pending; see
[`runtime-evidence-2026-09-03-c1-quest-stereo-smoke-build.md`](runtime-evidence-2026-09-03-c1-quest-stereo-smoke-build.md).
