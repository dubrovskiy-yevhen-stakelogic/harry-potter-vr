# Quest release build

The release contains the port and third-party libraries, not HP game assets,
decoded audio, saves or signing keys. Players import their own compatible US PC
data. The demo ends after the first Flipendo lesson.

## Tools

Windows PowerShell, Gradle 9.3.1, JDK 21, Android SDK 35, build-tools 35.0.0,
NDK 27.2.12479018 and CMake 3.22.1 are required. The build script accepts
`-AndroidSdk`, `-JavaDirectory` and `-Gradle` paths. Gradle uses its local cache;
`-AllowDependencyDownload` permits fetching missing build dependencies.

## Build and package

From the repository root, initialize a signing identity on the first build:

```powershell
.\BUILD-QUEST-RELEASE.ps1 -InitializeSigningKey
```

Later builds reuse that identity. Output directories must be new children of
`artifacts/`; existing releases are not overwritten:

```powershell
.\BUILD-QUEST-RELEASE.ps1 -OutputDirectory artifacts\quest-release-rebuild
```

The script builds the Gradle Release variant with native CMake Release
optimizations, adds dependency notices, aligns and signs the APK. It verifies
the signature, non-debuggable manifest, ARM64-only ABI and asset-free payload.
Version values are in `android/app/build.gradle`.

To create the Windows player ZIP, first build the host tools in Release, then:

```powershell
.\PACKAGE-QUEST-PLAYER.ps1 -ApkPath artifacts\quest-release-rebuild\HPVR-Quest-0.1.0-demo.apk -OutputDirectory artifacts\HPVR-Quest-Demo-rebuild
```

The packager reads the matching `RELEASE-METADATA.json`, checks the APK and
includes the installer, required helper tools, documentation and hashes. Its
default host-tool directory is `build/quest-host-tests`; override with
`-HostBuildDirectory`. Build and packaging do not install or launch the game.
Player instructions are in [PLAYER-INSTALL.md](../tools/release/PLAYER-INSTALL.md).

## Signing and upgrades

The persistent RSA-4096 PKCS12 key and DPAPI-protected credential file are stored
under ignored `local/signing/release/`. Access is restricted to the current
Windows account and SYSTEM. Back them up privately; never ship this directory.
DPAPI credentials depend on the Windows account/machine profile, so copying the
CLIXML file alone is not a portable recovery method. Keep secure offline key and
credential recovery separately. An incomplete key/credential pair is an error;
the script does not replace it automatically.

Release and development use `io.github.hpvr.quest` but different certificates.
**A release APK cannot update a debug-signed installation. Do not uninstall a
development build to bypass this conflict: saves, settings and imported data may
be lost.** Arrange a backup/migration first. Public updates must keep the original
release certificate.

## Data and licenses

The player installer imports packages and fingerprint-named PCM to
`/sdcard/Android/data/io.github.hpvr.quest/files/HP/`, including `Cache/Audio/`.
This uses external storage access, not debug-only `run-as`. Saves and VR settings
remain private. No game data belongs in the public ZIP.

[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) lists dependencies. The release
includes the installed OpenXR AAR and NDK license texts in APK `META-INF` and a
`THIRD-PARTY` folder. Aggregate NDK notices also describe toolchain components
that are not embedded in the app.

Before publication, test a fresh install with owned-data import on Quest,
including menus, saves, audio and cutscenes. Build/signature checks alone do not
test first-run behavior.
