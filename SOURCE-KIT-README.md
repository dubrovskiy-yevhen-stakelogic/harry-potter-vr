# Harry Potter VR: source-only snapshot

Start here for the current source kit. `README.md` also contains the project's
historical development notes; older milestones are not a current feature list.

This is a local snapshot of the original clean-room port code, including current
uncommitted and untracked production files. It is not a copy of the retail game
and is not a runnable game distribution. The demo covers the opening tutorial
through the first spell lesson. Development continues; news and feedback:

<https://discord.com/channels/747967102895390741/1543691482861408276>

## Included and excluded

Included: C/C++ runtime and tests, Vulkan shaders, Android build configuration,
the optional Rust PCVR probe, text tools, and project documentation.

Excluded: the original game's packages, textures, models, scripts extracted from
packages, speech, music, fonts, images, videos, derived asset dumps/caches,
executables, DLLs, APKs, native libraries, shader binaries, build directories,
third-party dependency caches, signing keys, saves, and local machine settings.
There is no bundled Unreal Engine or OpenHP1 source tree.

`SOURCE-SHA256.txt` lists every copied source file and its SHA-256. The exporter
checks an explicit file/extension allowlist, rejects links and binary/package
content disguised as source, and verifies copy hashes and a stable source
inventory. That mechanical check does not confer intellectual-property rights.

The kit is created by `EXPORT-SOURCE-KIT.ps1`. It never merges with or overwrites
an existing destination. Use a new folder for each subsequent snapshot:

```powershell
.\EXPORT-SOURCE-KIT.ps1 -AuditOnly
.\EXPORT-SOURCE-KIT.ps1 -DestinationPath 'C:\Dev\harry-potter-vr-source-kit-next'
```

The synthetic exporter boundary tests do not require game files or a device:

```powershell
.\tools\test-source-kit-export.ps1
```

They generate small test fixtures under ignored `local/` and verify exclusions,
copy hashes, binary-signature rejection, and safe destination/link handling.

## Licensing and provenance

The current project has no root `LICENSE` granting general redistribution rights.
This snapshot does not invent one and should not be described as an open-source
release until the owner chooses a license and completes a release review.
Harry Potter, Warner Bros. branding, and the original game content belong to
their respective rights holders; the project is not affiliated with them.

No third-party dependency implementation or dependency binary is copied into the
kit. Dependencies are obtained separately using the pinned build manifests. Their
licenses/notices continue to apply. Preserve the actual licenses and notices
from those dependencies when distributing a compiled build; this document does
not substitute for those license texts. The earlier OpenHP1 reference audit and
the decision not to vendor it are in `docs/runtime-foundation-decision.md`.

## Host build and tests (Windows)

Install Visual Studio 2022 C++ build tools with the Windows SDK, and CMake 3.25+
(the C++ code uses C++20). No game files, Android SDK, or Rust toolchain are
needed to compile the host targets and run their synthetic tests.

```powershell
cmake -S . -B build\quest-host-tests -G 'Visual Studio 17 2022' -A x64
cmake --build build\quest-host-tests --config Release --parallel
ctest --test-dir build\quest-host-tests -C Release --output-on-failure
```

Owned-data probes are separate and require a legally owned installation passed
as a command-line path. Never copy test data into `src`, `android`, or this kit.
Keep any locally generated output under ignored `local/` or an external folder.

## Android ARM64 / Quest build (Windows)

Pinned current inputs:

- Android SDK platform 35, Build Tools 35.0.0, NDK 27.2.12479018.
- Android CMake 3.22.1 and the NDK's Windows `glslc` shader compiler.
- JDK 21 in the existing Windows helper; Java source compatibility is 17.
- Android Gradle plugin 8.7.3; the existing workspace helper selects cached
  Gradle 9.3.1. There is no Gradle wrapper or Gradle distribution in this kit.
- `org.khronos.openxr:openxr_loader_for_android:1.1.43` from Maven Central,
  with Prefab headers and ARM64 loader resolved by Gradle.

The normal full APK build is Gradle, not `PACKAGE-QUEST-DEBUG.ps1` (the latter
replaces the native library inside an already built local APK). Configure
`JAVA_HOME` and `ANDROID_HOME` for your tools, then from the kit root:

```powershell
# Use your installed Gradle executable; the first build may resolve dependencies.
gradle --no-daemon -p android :app:assembleDebug
```

The existing `BUILD-QUEST-DEBUG.ps1` is a convenience helper for the author's
machine: SDK `C:\Dev\android-toolchain\sdk`, JDK
`C:\Dev\android-toolchain\jdk21`, and a cached Gradle 9.3.1. It deliberately uses
`--offline`; use the direct Gradle command when populating your own dependency
cache. The shader CMake logic currently targets Windows tool paths.

Output: `android\app\build\outputs\apk\debug\app-debug.apk`. The APK contains
the port code, not retail content. Compiling does not install or launch it.
APK validation and an actual Quest run are separate verification steps.

## Optional PCVR probe

`tools/xr-runtime-probe/rust-toolchain.toml` pins Rust 1.97.0. Cargo manifests pin
`ash`, `bytemuck`, `glam`, `openxr`, `wgpu`, and optional `cc`; `Cargo.lock`
records the full dependency resolution. Cargo obtains those packages separately.

```powershell
cargo test --locked --all-targets --manifest-path tools\xr-runtime-probe\Cargo.toml
cargo test --locked --all-targets --manifest-path tools\xr-runtime-probe\Cargo.toml --features gesture-projection
```

These test commands do not launch the retail game. Running a VR executable is
a separate action and needs a compatible OpenXR runtime and loader.

## Data and user state

Supply your own legally owned US PC release. Treat the retail install as
read-only. `IMPORT-QUEST-DATA.ps1` defaults to a plan; only its explicit `-Copy`
mode transfers selected owned data to a connected device. Inspect its inputs
before use. Audio/frontend preparation tools generate private derived data in
ignored `local/`, never in source or an APK. Do not share those outputs.

The snapshot has no save games or VR settings. Do not bundle your Documents
folder, device-private files, signing keystore, developer cache, or the original
game when sharing source. Installation, data import, and launch are deliberately
not performed by the source-kit exporter.
