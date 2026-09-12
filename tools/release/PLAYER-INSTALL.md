# Harry Potter VR — Quest demo installation

Version **0.1.1 alpha** is an unofficial standalone Quest 3 demo containing the
opening level and the Flipendo Challenge. Both maps are selected automatically.
The release contains the APK, an offline voice-recognition model and Windows
import tools, **not game assets, player recordings or saves**.

## Requirements

- Your own installed US PC copy of *Harry Potter and the Sorcerer's Stone*
  (2001). Other editions and modified packages are not supported by this demo.
- Windows with PowerShell 5.1 or newer and the Microsoft Visual C++ x64 runtime.
  If Windows reports missing `VCRUNTIME140` or `MSVCP140`, install Microsoft's
  redistributable, not individual DLLs from download sites.
- Quest 3 in developer mode, with USB debugging authorized for this PC.
- Android Platform Tools (`adb.exe`) are downloaded from Google if missing.
  On the first download, review the Android SDK terms and type `YES` to accept.
- FFmpeg is downloaded automatically if it is not already installed. The first
  download needs internet access (about 106 MB); later installs reuse its cache.
- Space on both PC and headset for the imported packages, decoded audio and
  prepared scenes (when supported by the bundled APK).

Extract the entire ZIP into a folder outside the PC game installation. Obtain it
from the author's trusted release channel: the hash manifest checks file
integrity, not the identity of an untrusted download.

## Install

Double-click `INSTALL-HPVR.cmd` and enter the game
folder containing `Maps`, `Textures`, `Sounds`, `Music` and `system`.
Missing FFmpeg and ADB are downloaded and cached automatically. The banner
must say `HPVR installer revision 5`. The selected maps should be `0, 1` for this
release. To use existing tools explicitly:

```powershell
.\INSTALL-HPVR.ps1 -GamePath 'C:\Program Files\HP' -AdbPath 'C:\Android\platform-tools\adb.exe' -FfmpegPath 'C:\ffmpeg\bin\ffmpeg.exe'
```

If PowerShell blocks the script, use the CMD wrapper with the same arguments:

```bat
INSTALL-HPVR.cmd -GamePath "C:\Program Files\HP" -AdbPath "C:\Android\platform-tools\adb.exe" -FfmpegPath "C:\ffmpeg\bin\ffmpeg.exe"
```

The wrapper's execution-policy override affects only its child process. For
multiple connected devices, add `-DeviceSerial` with the serial from `adb devices`.

`-FfmpegPath` and `-AdbPath` are optional: an explicit path takes priority, then PATH/common
locations, then the verified cache, then automatic download. Missing or mistyped
explicit paths are reported instead of silently downloading another copy.
Use `-NoToolDownload` to disable downloads; a verified cached copy still works.

The automatic download is the pinned Windows x64 FFmpeg 9.0.1 essentials build
from [gyan.dev](https://www.gyan.dev/ffmpeg/builds/), a distributor linked by
[ffmpeg.org](https://ffmpeg.org/download.html). Archive and executable SHA-256
hashes are verified before use. It is cached under
`%LOCALAPPDATA%\HPVR\Tools\ffmpeg-9.0.1`, together with its GPLv3 license and
README. No administrator access, global installation or PATH changes are made.
If the download fails, rerun the installer or supply `-FfmpegPath` manually.

ADB uses the versioned Windows Android Platform Tools 36.0.2 archive from
[Google](https://developer.android.com/tools/releases/platform-tools) (about 7 MB).
The archive, ADB executable, both required DLLs, license notices and version file
are SHA-256 verified and cached under `%LOCALAPPDATA%\HPVR\Tools\platform-tools-36.0.2`.
The installer does not install USB drivers or change PATH. Existing ADB and
offline preparation do not require a download or the license prompt.
If no authorized headset is found, enable developer mode, connect a USB data
cable, put on the headset and accept its USB debugging prompt, then rerun.

Preparation takes several minutes. The installer checks hashes, prepares the
required game packages, audio and supported scenes in a private working directory, installs the
APK, then imports data with SHA-256 readback. It does not edit or start the PC
game, import PC executables/DLLs, launch the Quest app, uninstall an app, or write
saves/settings.

Matching version 43 and newer kits include a scene-preparation tool. It converts
the selected level's geometry, textures and other supported scene data on the PC,
then independently verifies the generated cache before transfer. This moves
preparation work out of headset loading; it does not remove GPU initialization
or guarantee an instant first launch. Progress and per-level reports are shown
during installation. No additional game downloads are used.

This release's `release-manifest.json` declares `mapIds: [0, 1]`. A normal
double-click install imports the packages and audio for both levels, then
prepares and verifies both scene caches. No `-IncludeChallenge` option is needed.
Use the in-game level selection to start the Flipendo Challenge from its beginning.

Older release manifests without `mapIds` retain the original one-map default;
merely having a second map beside the first does not enable it. `-IncludeChallenge`
remains a compatibility option for older development builds that support the
challenge. It cannot add second-level support to the original one-map APK.
Older kits without a matching scene-preparation tool explicitly report that
they import packages/audio only and will rebuild geometry on the headset.
Do not mix a preparation tool or cache from another release into an old kit.
`-SkipScenePreparation` is available for troubleshooting; it deliberately keeps
the slower headset-preparation fallback and is not recommended for normal use.

After `INSTALL=PASS DATA_IMPORT=PASS`, open **Harry Potter VR Demo** in the
headset's Unknown Sources list.

## Existing builds

The app ID is `io.github.hpvr.quest`. If Android reports
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`, the installed app has a different signing
key. **Do not uninstall it to bypass the error: that can erase saves, settings
and imported data. Contact the author for migration.** The installer does not
uninstall apps or bypass downgrade protection.

The alpha APK uses Android version code 54. Updating an earlier public release
requires the same release certificate; a higher version number alone cannot
resolve a different signing key. Use the complete matching alpha installer kit
to add the second level's packages and prepared cache, not just its APK.

## Offline preparation and storage

To prepare and validate data without installing or using ADB:

```powershell
.\INSTALL-HPVR.ps1 -GamePath 'C:\Program Files\HP' -FfmpegPath 'C:\ffmpeg\bin\ffmpeg.exe' -PrepareOnly
```

`PREPARE=PASS` means local conversion succeeded, not installation. A later normal
install creates a fresh working folder; it does not reuse the previous one.
Use `-WorkRoot 'D:\HPVR-PrivateData'` for a different working location outside both
the release and the original game directories.

The default working location is `%LOCALAPPDATA%\HPVR\PrivateData\import-...`.
The installer prints the exact path. After a successful import, that one folder
may be removed to reclaim PC storage. It is retained after errors for diagnosis.
Do not distribute it: it contains your game packages, decoded audio and derived
scene data.

On Quest, data is stored under:

```text
/sdcard/Android/data/io.github.hpvr.quest/files/HP/
```

`HP/Cache/Audio/` contains source-fingerprinted 48 kHz signed 16-bit PCM: stereo
music, mono speech/effects. Import uses external storage and works with the
non-debuggable release; private saves are separate.

`HP/Cache/Scenes/map-0.hpvc` is the prepared first map and `map-1.hpvc` is the
prepared Flipendo Challenge. Both are generated and verified by default for
0.1.1 alpha. Only these
selected cache files are transferred; their SHA-256 hashes are checked on the
headset along with the other imported files. They remain private game-derived
data and are never included in the public release ZIP. If a cache is missing or
incompatible, the matching runtime can rebuild the scene from the owned game
packages instead; rerun the installer from a matching kit to restore preparation.

## Voice casting and privacy

Voice casting is optional. When enabled, it uses the headset microphone with
Android's microphone permission and recognizes spells locally; it does not need
an online speech service. The player build does not save voice recordings.
Private diagnostic recordings, test audio and diagnostic APKs are not part of
this release. The model's license notices are included in `THIRD-PARTY/`.

Share only the unmodified no-assets release ZIP, not prepared data or headset
backups. Feedback and updates:
[Discord](https://discord.com/channels/747967102895390741/1547254536203407390).
