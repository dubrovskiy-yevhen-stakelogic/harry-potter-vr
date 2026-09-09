# Harry Potter VR — source code

Source code for the native Meta Quest 3 port. See [README](README.md) for demo
features and controls.

## Layout

| Directory | Contents |
| --- | --- |
| `src/quest` | Portable gameplay, menus, gestures, saves and tests |
| `src/wand` | Game-package readers, geometry extraction and gesture processing |
| `android` | Android application, scene runtime, audio, OpenXR, Vulkan renderer and shaders |
| `tools/release` | Player installer and owned-data preparation |
| `tools/xr-runtime-probe` | Optional Rust PCVR development tool |
| `docs` | Technical documentation and dependency notices |

The source package contains no game assets, extracted data, executables, APKs,
bundled dependencies, signing keys, saves or local settings. Build dependencies
are obtained separately. Exported packages include `SOURCE-SHA256.txt` with source
file checksums.

## Windows build and tests

Requires Visual Studio 2022 C++ Build Tools, Windows SDK and CMake 3.25 or newer.
The code uses C++20. These tests do not require game data or a headset.

```powershell
cmake -S . -B build\host -G 'Visual Studio 17 2022' -A x64
cmake --build build\host --config Release --parallel
ctest --test-dir build\host -C Release --output-on-failure
```

## Android / Quest

Build dependencies:

- Android SDK 35, Build Tools 35.0.0, NDK 27.2.12479018.
- Android CMake 3.22.1 and the NDK's `glslc`.
- JDK 21, Gradle 9.3.1, Android Gradle Plugin 8.7.3.
- OpenXR Loader 1.1.43, resolved by Gradle from Maven Central.

Set `JAVA_HOME` and `ANDROID_HOME` for your installation. Gradle is not bundled;
the first build needs access to dependency repositories.

```powershell
gradle --no-daemon -p android :app:assembleDebug
```

The APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`.
`BUILD-QUEST-DEBUG.ps1` uses locally configured tool paths and an offline cache.
Use the Gradle command above for another environment.

Build a signed release with:

```powershell
.\BUILD-QUEST-RELEASE.ps1 -InitializeSigningKey
```

Use `-InitializeSigningKey` only when creating the first key. Keep the key private
and backed up: subsequent updates must use the same signing identity. Tool paths,
signing and packaging are covered in the [release build guide](docs/RELEASE-BUILD.md).

`PACKAGE-QUEST-PLAYER.ps1` creates the player installation kit. See the
[installation guide](tools/release/PLAYER-INSTALL.md) for setup and data import.
The APK contains no game data; players need their own US PC copy of the original.

## Exporting source

Export to a new directory. Existing directories are not overwritten.

```powershell
.\EXPORT-SOURCE-KIT.ps1 -AuditOnly
.\EXPORT-SOURCE-KIT.ps1 -DestinationPath 'D:\HPVR-Source'
.\tools\test-source-kit-export.ps1
```

Keep game data and generated audio outside the source package, for example in
the ignored `local/` directory. Do not add them to public archives.

## Optional PCVR tests

The Rust version is pinned in `tools/xr-runtime-probe/rust-toolchain.toml`.

```powershell
cargo test --locked --all-targets --manifest-path tools\xr-runtime-probe\Cargo.toml
cargo test --locked --all-targets --manifest-path tools\xr-runtime-probe\Cargo.toml --features gesture-projection
```

## Documentation and licenses

- [Architecture](docs/architecture.md)
- [Gesture processing](docs/wand-gesture-contract.md)
- [Third-party components and licenses](docs/THIRD-PARTY-NOTICES.md)

The repository does not currently include a root `LICENSE` granting general reuse
or redistribution rights for its source. Third-party licenses apply separately.
Original game data belongs to its respective rights holders and is not included.
