# Harry Potter VR — source code

This checkout predates the current demo described in [README](README.md). Its
cutscenes use the theatrical camera without a first-person toggle. Defaults in
this code are Render Scale 100%, SSR 35 and Original difficulty; the release
settings listed in README do not change this version's behavior.

## Layout

| Directory | Contents |
| --- | --- |
| `src/quest` | Scene, story, interface, audio and gameplay tests |
| `src/wand` | Package readers, geometry and gesture processing |
| `android` | Android application, OpenXR, Vulkan and shaders |
| `tools/xr-runtime-probe` | Optional Rust PCVR tool |
| `docs` | Architecture and gesture documentation |

Source distributions exclude game assets, extracted data, executables, APKs,
dependencies, signing keys, saves and local settings. Build dependencies are
obtained separately. `SOURCE-SHA256.txt` lists file checksums.

## Windows build and tests

Install Visual Studio 2022 C++ Build Tools, the Windows SDK and CMake 3.25 or
newer. The code uses C++20. These tests need neither game data nor a headset.

```powershell
cmake -S . -B build\host -G 'Visual Studio 17 2022' -A x64
cmake --build build\host --config Release --parallel
ctest --test-dir build\host -C Release --output-on-failure
```

## Android / Quest

Required tools: Android SDK 35, Build Tools 35.0.0, NDK 27.2.12479018, Android
CMake 3.22.1, JDK 21 and Gradle 9.3.1. The project uses Android Gradle Plugin
8.7.3 and OpenXR Loader 1.1.43. Shaders use the NDK's `glslc` compiler.

Set `JAVA_HOME` and `ANDROID_HOME`. Gradle is not bundled; the first build needs
access to the dependency repositories.

```powershell
gradle --no-daemon -p android :app:assembleDebug
```

Output: `android/app/build/outputs/apk/debug/app-debug.apk`.
`BUILD-QUEST-DEBUG.ps1` uses local tool paths and an offline cache; use Gradle
directly for another environment. This checkout does not contain the public
release packaging scripts or player installer.

## Game data

Running the port requires your own US PC copy. `IMPORT-QUEST-DATA.ps1` shows an
import plan by default; `-Copy` transfers selected data to a connected Quest.
`PREPARE-QUEST-AUDIO.ps1` and `PREPARE-QUEST-FRONTEND.ps1` prepare music, dialogue
and interface data from that copy. The original installation stays unchanged.
Do not include prepared game data in source distributions or public archives.

## Exporting source

Choose a new destination directory; existing folders are not overwritten.

```powershell
.\EXPORT-SOURCE-KIT.ps1 -AuditOnly
.\EXPORT-SOURCE-KIT.ps1 -DestinationPath 'D:\HPVR-Source'
.\tools\test-source-kit-export.ps1
```

## Optional PCVR tests

The Rust version is pinned in `tools/xr-runtime-probe/rust-toolchain.toml`.

```powershell
cargo test --locked --all-targets --manifest-path tools\xr-runtime-probe\Cargo.toml
cargo test --locked --all-targets --manifest-path tools\xr-runtime-probe\Cargo.toml --features gesture-projection
```

## Documentation and licensing

- [Architecture](docs/architecture.md)
- [Wand and gestures](docs/wand-gesture-contract.md)

There is currently no root `LICENSE` granting general rights to use or redistribute
the source. Third-party dependency licenses apply separately. Original game data
belongs to its rights holders and is not included.
