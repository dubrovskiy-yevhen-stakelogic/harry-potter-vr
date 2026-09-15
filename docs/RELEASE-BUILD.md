# Quest release build

The current source targets **0.1.3-alpha**, Android version code **73**,
with the opening level, [Flipendo Challenge](FLIPENDO-CHALLENGE.md), Broomstick
Training and Alohomora / Charms.
The release contains the port, third-party libraries and the licensed offline voice model,
not HP game assets, decoded game audio, user recordings, saves or signing keys.
Players import their own compatible US PC data for the maps declared by the release.

Each newly restored level increments the last numeric version component:
0.1.2, then 0.1.3, 0.1.4, and so on. The alpha label remains while the port is
in early testing. Android version codes increase independently for new APKs.

## Tools

Windows PowerShell, Gradle 9.3.1, JDK 21, Android SDK 35, build-tools 35.0.0,
NDK 27.2.12479018 and CMake 3.22.1 are required. The build script accepts
`-AndroidSdk`, `-JavaDirectory` and `-Gradle` paths. Gradle uses its local cache;
`-AllowDependencyDownload` permits fetching missing build dependencies.

## Build and package

For an update, reuse the existing release signing identity. From the repository
root, build into a new artifact directory:

```powershell
.\tools\workspace\BUILD-QUEST-RELEASE.ps1 -OutputDirectory artifacts\quest-release-0.1.3-alpha
```

Only a project's first release with no identity should use
`-InitializeSigningKey`. Do not generate a replacement key for an update.
Output directories must be new children of `artifacts/`; existing releases are
not overwritten. For a rebuild, choose a new output directory and pass its APK
path explicitly to the packager.

The script builds the Gradle Release variant with native CMake Release
optimizations, adds dependency notices, aligns and signs the APK. It verifies
the signature, non-debuggable manifest, ARM64-only ABI and asset-free payload.
Version values are read from `android/app/build.gradle`. The APK is named
`HPVR-Quest-<versionName>.apk`; an omitted output directory generates a fresh
versioned, timestamped directory under `artifacts/`.

Build the matching Windows Release host helpers from this checkout, then create
the player ZIP. This example uses the main `build` tree:

```powershell
cmake --build build --config Release --target hpvr_hp1_package_graph hpvr_hp1_sound_probe hpvr_quest_frontend_probe hpvr_quest_intro_probe hpvr_quest_prepare_assets
.\tools\workspace\PACKAGE-QUEST-PLAYER.ps1 -HostBuildDirectory build `
  -ApkPath artifacts\quest-release-0.1.3-alpha\HPVR-Quest-0.1.3-alpha.apk `
  -OutputDirectory artifacts\HPVR-Quest-Demo-0.1.3-alpha
```

The packager reads the matching `RELEASE-METADATA.json`, checks the APK and
includes the installer, required helper tools, documentation and hashes. Its
input APK and output directory must be explicit so an older release cannot be
packaged accidentally. The matching metadata and APK must come from the same build.
Its default host-tool directory is `build/quest-host-tests`; the explicit
`-HostBuildDirectory build` above selects the main tree instead. Build and
packaging do not install or launch the game.
Player instructions are in [PLAYER-INSTALL.md](../tools/release/PLAYER-INSTALL.md).

The four-map manifest declares `mapIds: [0, 1, 2, 3]`. The installer uses that
selection for package closures, dialogue/music enumeration and scene
preparation. `map-0.hpvc` through `map-3.hpvc` are generated and independently
verified on the player's PC; they are never packaged into the ZIP. Historical
manifests without map metadata retain map 0 only, with `-IncludeChallenge`
available for compatible older development APKs.

Before packaging, run the synthetic installer tests:

```powershell
.\tools\release\TEST-PLAYER-INSTALL.ps1
```

After packaging, validate the actual bundled helpers against an owned copy with
the bundled `INSTALL-HPVR.ps1 -GamePath '<owned US PC game folder>' -PrepareOnly`.
This prepares all declared maps without installing or accessing a headset.
After device import, revision 7 grants directory read/traverse and file read
permissions to other UIDs. APK version code 72 and newer verifies actual file
reads through a permission-protected background receiver, without starting the
VR activity. Older APKs use mode-bit verification; emulated storage can mask
these bits and require an APK upgrade to verify access reliably.
It does not change ownership, grant world-write access, or touch saves/settings.

## Signing and upgrades

The persistent RSA-4096 PKCS12 key and DPAPI-protected credential file are stored
under ignored `local/signing/release/`. Access is restricted to the current
Windows account and SYSTEM. Back them up privately; never ship this directory.
DPAPI credentials depend on the Windows account/machine profile, so copying the
CLIXML file alone is not a portable recovery method. Keep secure offline key and
credential recovery separately. An incomplete key/credential pair is an error;
the script does not replace it automatically.

Release-signed and debug-signed builds use `io.github.hpvr.quest` but different
certificates. A development version can use the existing release key, too.
**A release APK cannot update a debug-signed installation. Do not uninstall a
development build to bypass this conflict: saves, settings and imported data may
be lost.** Arrange a backup/migration first. Public updates must keep the original
release certificate.

Version code 57 is newer than the previous public demo, but certificate equality
must also be checked against the previous release's `RELEASE-METADATA.json`.
The player installer uses a normal in-place `adb install -r`, without downgrade
or uninstall flags. Android rejects an incompatible signer before data import.
Do not install the public release over a debug-signed development test just to
validate packaging.

## Data and licenses

The player installer imports packages, fingerprint-named PCM and all selected
scene caches to
`/sdcard/Android/data/io.github.hpvr.quest/files/HP/`, including `Cache/Audio/`.
This uses external storage access, not debug-only `run-as`. Saves and VR settings
remain private. No game data belongs in the public ZIP.

[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) lists dependencies. The release
includes the installed OpenXR AAR and NDK license texts in APK `META-INF` and a
`THIRD-PARTY` folder. The voice model, keyword/token files and their license
notices are pinned by `tools/voice/VOICE-ASSETS.psd1`; payload verification
rejects unexpected or modified voice files. Aggregate NDK notices also describe
toolchain components that are not embedded in the app.

Normal and Release builds compile without voice-recording diagnostics. The
separate `voiceDiagnostic` APK is for explicitly authorized local testing only.
The player packager rejects its version/manifest and retained native diagnostic
marker even when an explicit APK path is supplied. There is no override in the
player packager. Do not copy `local/`, private recordings, diagnostic grants,
prepared owned data or signing material into either distribution.

Before publication, test a fresh install with owned-data import on Quest,
including menus, saves, audio and cutscenes. Build/signature checks alone do not
test first-run behavior.
