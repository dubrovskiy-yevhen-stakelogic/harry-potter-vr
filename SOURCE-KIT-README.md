# Harry Potter VR — source code

Source code for **0.1.1 Alpha**, the native Meta Quest 3 port. It includes the
opening tutorials, Flipendo lesson and Flipendo Challenge. See [README](README.md)
for features, controls and limitations.

## Layout

| Directory | Contents |
| --- | --- |
| `src/quest` | Portable gameplay, menus, gestures, saves and tests |
| `src/wand` | Game-package readers, geometry extraction and gesture processing |
| `android` | Android application, scene runtime, audio, OpenXR, Vulkan renderer and shaders |
| `tools/release` | Player installer and owned-data preparation |
| `tools/voice` | Pinned offline speech dependencies, build scripts and tests |
| `tools/xr-runtime-probe` | Optional Rust PCVR development tool |
| `docs` | Technical documentation and dependency notices |

The source package contains no game assets, extracted data, executables, APKs,
bundled dependencies, signing keys, saves, microphone recordings or local settings.
Exported packages include `SOURCE-SHA256.txt` with source-file checksums.

## Windows build and tests

Requires Visual Studio 2022 C++ Build Tools, Windows SDK and CMake 3.25 or newer.
The code uses C++20. The default portable tests do not need game data or a headset.

```powershell
cmake -S . -B build\host -G 'Visual Studio 17 2022' -A x64
cmake --build build\host --config Release --parallel
ctest --test-dir build\host -C Release --output-on-failure
```

Owned-data probes additionally need a compatible US PC installation. They must
write generated assets outside that installation and outside the source package.
Neural voice checks use separate dependencies and are described in the
[voice build guide](tools/voice/README.md).

## Android / Quest

Build dependencies:

- Android SDK 35, Build Tools 35.0.0, NDK 27.2.12479018.
- Android CMake 3.22.1 and the NDK's `glslc`.
- JDK 21, Gradle 9.3.1, Android Gradle Plugin 8.7.3.
- OpenXR Loader 1.1.43, resolved by Gradle from Maven Central.

Set `JAVA_HOME` and `ANDROID_HOME` for your installation. Gradle is not bundled;
the first build needs access to dependency repositories.

Prepare the pinned offline speech model and CPU-only ARM64 runtime before the
first Android build. Both preparation scripts support `-Offline` once their
dependencies are cached.

```powershell
.\tools\voice\FETCH-VOICE-DEPENDENCIES.ps1
.\tools\voice\BUILD-NEURAL-VOICE-RUNTIME.ps1 `
    -NdkPath "$env:ANDROID_HOME\ndk\27.2.12479018" `
    -CMakePath "$env:ANDROID_HOME\cmake\3.22.1\bin\cmake.exe" `
    -NinjaPath "$env:ANDROID_HOME\cmake\3.22.1\bin\ninja.exe"
gradle --no-daemon -p android :app:assembleDebug
```

The APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`.
`BUILD-QUEST-DEBUG.ps1` uses locally configured tool paths and an offline cache;
use the Gradle command above for another environment.

For a signed release:

```powershell
.\BUILD-QUEST-RELEASE.ps1 -InitializeSigningKey
```

Use `-InitializeSigningKey` only when creating the first key. Keep the key private
and backed up: updates must use the same signing identity. Tool paths, signing and
packaging are covered in the [release build guide](docs/RELEASE-BUILD.md).

`PACKAGE-QUEST-PLAYER.ps1` creates the player installation kit. It contains the
APK and importer, not game data. The [installer](tools/release/PLAYER-INSTALL.md)
prepares the two supported maps from the player's own US PC copy.

Normal debug and all release APKs exclude microphone-recording diagnostics.
Do not distribute the separate opt-in diagnostic build.

## Exporting source

Export to a new directory. Existing directories are not overwritten.

```powershell
.\EXPORT-SOURCE-KIT.ps1 -AuditOnly
.\EXPORT-SOURCE-KIT.ps1 -DestinationPath 'D:\HPVR-Source'
.\tools\test-source-kit-export.ps1
```

The exporter copies current source files, including uncommitted changes.
Keep generated game data, audio, captures and signing material outside public
archives; the ignored `local/` directory is intended for private outputs.

## Optional PCVR tests

The Rust version is pinned in `tools/xr-runtime-probe/rust-toolchain.toml`.

```powershell
cargo test --locked --all-targets --manifest-path tools\xr-runtime-probe\Cargo.toml
cargo test --locked --all-targets --manifest-path tools\xr-runtime-probe\Cargo.toml --features gesture-projection
```

## Licences

[Third-party notices](docs/THIRD-PARTY-NOTICES.md) identify the dependencies and
their licences. The repository does not currently include a root `LICENSE`
granting general reuse or redistribution rights for its own source.
Original game data belongs to its respective rights holders and is not included.
