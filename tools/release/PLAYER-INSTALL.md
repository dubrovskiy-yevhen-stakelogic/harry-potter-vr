# Harry Potter VR — Quest demo installation

An unofficial standalone Quest 3 demo through the first Flipendo lesson.
The release contains the APK and Windows import tools, **not game assets**.

## Requirements

- Your own installed US PC copy of *Harry Potter and the Sorcerer's Stone*
  (2001). Other editions and modified packages are not supported by this demo.
- Windows with PowerShell 5.1 or newer and the Microsoft Visual C++ x64 runtime.
  If Windows reports missing `VCRUNTIME140` or `MSVCP140`, install Microsoft's
  redistributable, not individual DLLs from download sites.
- Quest 3 in developer mode, with USB debugging authorized for this PC.
- Android platform-tools (`adb.exe`), on PATH or supplied with `-AdbPath`.
- FFmpeg is downloaded automatically if it is not already installed. The first
  download needs internet access (about 106 MB); later installs reuse its cache.
- Space on both PC and headset for the imported packages and decoded audio.

Extract the entire ZIP into a folder outside the PC game installation. Obtain it
from the author's trusted release channel: the hash manifest checks file
integrity, not the identity of an untrusted download.

## Install

If ADB is on PATH, double-click `INSTALL-HPVR.cmd` and enter the game
folder containing `Maps`, `Textures`, `Sounds`, `Music` and `system`.
Otherwise, run this in the extracted release folder:

```powershell
.\INSTALL-HPVR.ps1 -GamePath 'C:\Program Files\HP' -AdbPath 'C:\Android\platform-tools\adb.exe' -FfmpegPath 'C:\ffmpeg\bin\ffmpeg.exe'
```

If PowerShell blocks the script, use the CMD wrapper with the same arguments:

```bat
INSTALL-HPVR.cmd -GamePath "C:\Program Files\HP" -AdbPath "C:\Android\platform-tools\adb.exe" -FfmpegPath "C:\ffmpeg\bin\ffmpeg.exe"
```

The wrapper's execution-policy override affects only its child process. For
multiple connected devices, add `-DeviceSerial` with the serial from `adb devices`.

`-FfmpegPath` is optional: an explicit path takes priority, then PATH/common
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

Preparation takes several minutes. The installer checks hashes, prepares the
required game packages and audio in a private working directory, installs the
APK, then imports data with SHA-256 readback. It does not edit or start the PC
game, import PC executables/DLLs, launch the Quest app, uninstall an app, or write
saves/settings.

After `INSTALL=PASS DATA_IMPORT=PASS`, open **Harry Potter VR Demo** in the
headset's Unknown Sources list.

## Existing builds

The app ID is `io.github.hpvr.quest`. If Android reports
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`, the installed app has a different signing
key. **Do not uninstall it to bypass the error: that can erase saves, settings
and imported data. Contact the author for migration.** The installer does not
uninstall apps or bypass downgrade protection.

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
Do not distribute it: it contains your game packages and decoded audio.

On Quest, data is stored under:

```text
/sdcard/Android/data/io.github.hpvr.quest/files/HP/
```

`HP/Cache/Audio/` contains source-fingerprinted 48 kHz signed 16-bit PCM: stereo
music, mono speech/effects. Import uses external storage and works with the
non-debuggable release; private saves are separate.

Share only the unmodified no-assets release ZIP, not prepared data or headset
backups. Feedback and updates:
[Discord](https://discord.com/channels/747967102895390741/1547254536203407390).
