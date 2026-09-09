#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GamePath,
    [string]$AdbPath,
    [string]$FfmpegPath,
    [switch]$NoToolDownload,
    [string]$DeviceSerial,
    [string]$WorkRoot,
    [switch]$PrepareOnly,
    [switch]$PromptForGamePath,
    [switch]$LibraryOnly
)

# This script belongs at the release root, beside release-manifest.json.
# Its output is private, locally derived game data and is NEVER release content.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-HpvrFullPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'An explicit non-empty path is required.' }
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -eq [IO.Path]::GetPathRoot($full)) { return $full }
    return $full.TrimEnd('\', '/')
}

function Test-HpvrWithin([string]$Child, [string]$Parent) {
    $childFull = Get-HpvrFullPath $Child
    $parentFull = Get-HpvrFullPath $Parent
    return $childFull.Equals($parentFull, [StringComparison]::OrdinalIgnoreCase) -or
        $childFull.StartsWith($parentFull.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-HpvrNoLinks([string]$Path) {
    $current = Get-HpvrFullPath $Path
    while (-not [string]::IsNullOrEmpty($current)) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Symbolic links/junctions are not accepted: $current"
            }
        }
        $next = [IO.Path]::GetDirectoryName($current)
        if ($next -eq $current) { break }
        $current = $next
    }
}

function Get-HpvrRelative([string]$Path, [string]$Root) {
    $full = Get-HpvrFullPath $Path
    $base = Get-HpvrFullPath $Root
    if (-not (Test-HpvrWithin $full $base) -or $full -eq $base) { throw "Path escapes its root: $full" }
    return $full.Substring($base.Length + 1).Replace('\', '/')
}

function Assert-HpvrSafeRelative([string]$Relative) {
    if ($Relative -notmatch '^[A-Za-z0-9_-][A-Za-z0-9_. /-]*$' -or
        $Relative.Contains('//') -or @($Relative.Split('/') | Where-Object { $_ -eq '.' -or $_ -eq '..' -or $_.EndsWith('.') -or $_.EndsWith(' ') }).Count) {
        throw "Unsafe relative path: $Relative"
    }
}

function Get-HpvrTool([string]$Explicit, [string]$Name, [string[]]$Candidates, [switch]$Optional) {
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        $path = Get-HpvrFullPath $Explicit
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "$Name was not found: $path" }
        return $path
    }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return Get-HpvrFullPath $candidate
        }
    }
    if ($Optional) { return $null }
    $option = if ($Name -eq 'adb.exe') { '-AdbPath' } else { '-FfmpegPath' }
    throw "$Name was not found. Supply its full path with $option."
}

function Get-HpvrFfmpegSpec {
    # Windows essentials build linked by ffmpeg.org. Pin both archive and EXE;
    # never execute an unverified download or fetch a moving 'latest' release.
    return [pscustomobject]@{
        Version = '9.0.1'
        Url = 'https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.1-essentials_build.zip'
        ArchiveSha256 = 'FEC81AE03971D9DD4BE3EBE02E263BD2EC1D789483F931BDBA5F5715E65DA2E9'
        ExeSha256 = '72A489ECCD008C2EC2C0A5856C5C75BC3D8BBFA90166C4566865C246445E6AA3'
        Prefix = 'ffmpeg-9.0.1-essentials_build/'
    }
}

function Save-HpvrFfmpegDownload([string]$Url, [string]$Destination) {
    $protocol = [Net.ServicePointManager]::SecurityProtocol
    $progress = $ProgressPreference
    try {
        [Net.ServicePointManager]::SecurityProtocol = $protocol -bor [Net.SecurityProtocolType]::Tls12
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Destination -TimeoutSec 180
    } finally {
        [Net.ServicePointManager]::SecurityProtocol = $protocol
        $ProgressPreference = $progress
    }
}

function Get-HpvrFfmpeg([string]$Explicit, [string[]]$Candidates, [string]$CacheRoot, [switch]$NoDownload) {
    $found = Get-HpvrTool $Explicit 'ffmpeg.exe' $Candidates -Optional
    if ($found) { return $found }
    $spec = Get-HpvrFfmpegSpec
    if ([string]::IsNullOrWhiteSpace($CacheRoot)) {
        if (-not $env:LOCALAPPDATA) { throw 'LOCALAPPDATA is unavailable. Supply -FfmpegPath to an installed ffmpeg.exe.' }
        $CacheRoot = Join-Path $env:LOCALAPPDATA ('HPVR\Tools\ffmpeg-' + $spec.Version)
    }
    $cache = Get-HpvrFullPath $CacheRoot
    Assert-HpvrNoLinks $cache
    $exe = Join-Path $cache 'ffmpeg.exe'
    Assert-HpvrNoLinks $exe
    if ((Test-Path -LiteralPath $exe -PathType Leaf) -and
        (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -eq $spec.ExeSha256) {
        Write-Host "Using verified cached FFmpeg $($spec.Version): $exe"
        return $exe
    }
    if ($NoDownload) { throw 'FFmpeg is missing or its cache is invalid; automatic downloads are disabled. Supply -FfmpegPath.' }
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'Automatic FFmpeg setup requires 64-bit Windows. Supply a compatible -FfmpegPath.' }
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $nonce = [Guid]::NewGuid().ToString('N')
    $zipPath = Join-Path $cache ('download-' + $nonce + '.zip')
    $pendingExe = Join-Path $cache ('ffmpeg-' + $nonce + '.pending')
    Write-Host "FFmpeg was not found. Downloading FFmpeg $($spec.Version) from gyan.dev (about 106 MB)."
    Write-Host 'It will be cached for future installs. No administrator rights or PATH changes are needed.'
    try {
        Save-HpvrFfmpegDownload $spec.Url $zipPath
        if ((Get-Item -LiteralPath $zipPath).Length -gt 250MB -or
            (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash -ne $spec.ArchiveSha256) {
            throw 'Downloaded archive failed its pinned SHA-256 check.'
        }
        Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
        try {
            # Extract only exact named entries, never arbitrary ZIP paths.
            foreach ($item in @(@('bin/ffmpeg.exe', $pendingExe), @('LICENSE', (Join-Path $cache 'LICENSE')),
                    @('README.txt', (Join-Path $cache 'README.txt')))) {
                $name = $spec.Prefix + $item[0]
                $entries = @($archive.Entries | Where-Object { $_.FullName -ceq $name })
                $limit = if ($item[0] -eq 'bin/ffmpeg.exe') { 200MB } else { 2MB }
                if ($entries.Count -ne 1 -or $entries[0].Length -le 0 -or $entries[0].Length -gt $limit) {
                    throw "Missing, duplicate or oversized archive entry: $name"
                }
                Assert-HpvrNoLinks $item[1]
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], $item[1], $true)
            }
        } finally { $archive.Dispose() }
        if ((Get-FileHash -LiteralPath $pendingExe -Algorithm SHA256).Hash -ne $spec.ExeSha256) {
            throw 'Extracted FFmpeg failed its pinned SHA-256 check.'
        }
        Assert-HpvrNoLinks $exe
        Move-Item -LiteralPath $pendingExe -Destination $exe -Force
        Write-Host "FFmpeg is ready: $exe"
        return $exe
    } catch {
        throw "Automatic FFmpeg setup failed: $($_.Exception.Message) Re-run to retry, or download FFmpeg manually and supply -FfmpegPath."
    } finally {
        foreach ($temporary in @($zipPath, $pendingExe)) {
            if ((Test-HpvrWithin $temporary $cache) -and $temporary -ne $cache -and (Test-Path -LiteralPath $temporary -PathType Leaf)) {
                Assert-HpvrNoLinks $temporary
                Remove-Item -LiteralPath $temporary -Force
            }
        }
    }
}

function Get-HpvrAdbSpec {
    # Official versioned Google archive; pin ADB and all adjacent dependencies.
    return [pscustomobject]@{
        Version = '36.0.2'
        Url = 'https://dl.google.com/android/repository/platform-tools_r36.0.2-win.zip'
        ArchiveSha256 = 'B024D4F319D6AD3004DE1BA7B96A5C7C5F3512E8B14126308D598B4AB93DCEAD'
        Files = [ordered]@{
            'adb.exe' = '56656270DA132F44E9CB4FB86A12BA965635C80423D43DCDD944D9FEC4AB4622'
            'AdbWinApi.dll' = 'A00CF631DD12C82561FFCCEFDB1A99A27C527253B04F06CA8FC1BB86BA2148C4'
            'AdbWinUsbApi.dll' = 'E4D72D5BA3BF4B027F1B7A3781AAE0B04712C1A52244BEA277FB57DD75A85702'
            'NOTICE.txt' = 'BFEDFB7B22C5D204BAECBCCFBB9A6DE6E848EF19F9318088F15769BFBF4B8F79'
            'source.properties' = '6F16C7815EE0A1B820CA24C8FDAD8E26F07DA492B22C2B370C6DB136425A2136'
        }
    }
}

function Save-HpvrAdbDownload([string]$Url, [string]$Destination) {
    Save-HpvrFfmpegDownload $Url $Destination
}

function Get-HpvrAdb([string]$Explicit, [string[]]$Candidates, [string]$CacheRoot, [switch]$NoDownload) {
    $found = Get-HpvrTool $Explicit 'adb.exe' $Candidates -Optional
    if ($found) { return $found }
    $spec = Get-HpvrAdbSpec
    if ([string]::IsNullOrWhiteSpace($CacheRoot)) {
        if (-not $env:LOCALAPPDATA) { throw 'LOCALAPPDATA is unavailable. Supply -AdbPath to an installed adb.exe.' }
        $CacheRoot = Join-Path $env:LOCALAPPDATA ('HPVR\Tools\platform-tools-' + $spec.Version)
    }
    $cache = Get-HpvrFullPath $CacheRoot
    Assert-HpvrNoLinks $cache
    $valid = $true
    foreach ($name in $spec.Files.Keys) {
        $path = Join-Path $cache $name
        Assert-HpvrNoLinks $path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $spec.Files[$name]) { $valid = $false }
    }
    $exe = Join-Path $cache 'adb.exe'
    if ($valid) {
        Write-Host "Using verified cached Android Platform Tools $($spec.Version): $exe"
        return $exe
    }
    if ($NoDownload) { throw 'ADB is missing or its cache is invalid; automatic downloads are disabled. Supply -AdbPath.' }
    Write-Host 'ADB was not found. Android Platform Tools can be downloaded from Google (about 7 MB).'
    Write-Host 'Android SDK terms: https://developer.android.com/studio/terms'
    if ((Read-Host 'Read the terms above. Type YES to accept and download, or cancel and supply -AdbPath') -cne 'YES') {
        throw 'Android Platform Tools download cancelled. Install it yourself and supply -AdbPath.'
    }
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $nonce = [Guid]::NewGuid().ToString('N')
    $zipPath = Join-Path $cache ('download-' + $nonce + '.zip')
    $temporary = @($zipPath)
    try {
        Save-HpvrAdbDownload $spec.Url $zipPath
        if ((Get-Item -LiteralPath $zipPath).Length -gt 50MB -or
            (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash -ne $spec.ArchiveSha256) {
            throw 'Downloaded Android Platform Tools archive failed its pinned SHA-256 check.'
        }
        Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
        $archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
        $pending = [ordered]@{}
        try {
            foreach ($name in $spec.Files.Keys) {
                $entries = @($archive.Entries | Where-Object { $_.FullName -ceq ('platform-tools/' + $name) })
                if ($entries.Count -ne 1 -or $entries[0].Length -le 0 -or $entries[0].Length -gt 20MB) {
                    throw "Missing, duplicate or oversized Android Platform Tools entry: $name"
                }
                $path = Join-Path $cache ($nonce + '-' + $name + '.pending')
                $temporary += $path
                Assert-HpvrNoLinks $path
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], $path, $false)
                if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $spec.Files[$name]) {
                    throw "Extracted Android Platform Tools file failed its pinned SHA-256 check: $name"
                }
                $pending[$name] = $path
            }
        } finally { $archive.Dispose() }
        # Admit files only after every dependency passes verification.
        foreach ($name in $pending.Keys) {
            $destination = Join-Path $cache $name
            Assert-HpvrNoLinks $destination
            Move-Item -LiteralPath $pending[$name] -Destination $destination -Force
        }
        Write-Host "ADB is ready: $exe (no administrator access or PATH changes)."
        return $exe
    } catch {
        throw "Automatic ADB setup failed: $($_.Exception.Message) Re-run to retry, or supply -AdbPath manually."
    } finally {
        foreach ($path in $temporary) {
            if ((Test-HpvrWithin $path $cache) -and $path -ne $cache -and (Test-Path -LiteralPath $path -PathType Leaf)) {
                Assert-HpvrNoLinks $path
                Remove-Item -LiteralPath $path -Force
            }
        }
    }
}

function Read-HpvrReleaseManifest([string]$Root) {
    $manifestPath = Join-Path $Root 'release-manifest.json'
    Assert-HpvrNoLinks $manifestPath
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Missing release-manifest.json. Extract the complete release ZIP first.' }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or $manifest.packageName -ne 'io.github.hpvr.quest' -or $manifest.apk -notmatch '^[A-Za-z0-9_.-]+\.apk$') {
        throw 'Unsupported or invalid release manifest.'
    }
    $seen = @{}
    foreach ($entry in $manifest.files) {
        Assert-HpvrSafeRelative $entry.path
        if ($entry.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or $seen.ContainsKey($entry.path)) { throw "Invalid or duplicate hash entry: $($entry.path)" }
        $path = Join-Path $Root $entry.path
        Assert-HpvrNoLinks $path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Incomplete release: $($entry.path)" }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Release hash mismatch: $($entry.path). Download/extract the release again." }
        $seen[$entry.path] = $entry.sha256
    }
    foreach ($required in @($manifest.apk, 'tools/hpvr_hp1_package_graph.exe', 'tools/hpvr_hp1_sound_probe.exe',
            'tools/hpvr_quest_frontend_probe.exe', 'tools/hpvr_quest_intro_probe.exe')) {
        if (-not $seen.ContainsKey($required)) { throw "Unverified release component: $required" }
    }
    return $manifest
}

function Invoke-HpvrChecked([string]$Executable, [string[]]$Arguments, [string]$Label) {
    # Native stderr remains visible (FFmpeg uses it for errors); never interpret it as script text.
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Label failed (exit $LASTEXITCODE). No game has been launched." }
}

function Get-HpvrOwnedInput([string]$Root, [string]$Path) {
    $full = Get-HpvrFullPath $Path
    if (-not (Test-HpvrWithin $full $Root)) { throw "Owned-data dependency escapes the selected installation: $full" }
    Assert-HpvrNoLinks $full
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "Required owned game file is missing: $full" }
    $relative = Get-HpvrRelative $full $Root
    Assert-HpvrSafeRelative $relative
    $parts = $relative.Split('/')
    if ($parts.Count -ne 2) { throw "Unexpected owned-data layout: $relative" }
    $directories = @{ system = 'system'; maps = 'Maps'; textures = 'Textures'; sounds = 'Sounds'; music = 'Music' }
    $extensions = @{ system = @('.u', '.int'); maps = @('.unr'); textures = @('.utx'); sounds = @('.uax'); music = @('.umx') }
    $directory = $parts[0].ToLowerInvariant()
    if (-not $directories.ContainsKey($directory) -or $extensions[$directory] -notcontains [IO.Path]::GetExtension($parts[1]).ToLowerInvariant()) {
        throw "Not an allowed game-data package: $relative"
    }
    return [pscustomobject]@{ Source = $full; Relative = $directories[$directory] + '/' + $parts[1] }
}

function Assert-HpvrPcm([string]$Path, [int]$Channels) {
    if ($Channels -ne 1 -and $Channels -ne 2) { throw 'PCM channels must be 1 or 2.' }
    $file = Get-Item -LiteralPath $Path
    if ($file.Length -lt 960 -or $file.Length -gt 57600000 -or ($file.Length % (2 * $Channels)) -ne 0) {
        throw "Invalid decoded PCM size: $Path ($($file.Length) bytes)"
    }
    $data = [IO.File]::ReadAllBytes($file.FullName)
    foreach ($sampleByte in $data) { if ($sampleByte -ne 0) { return } }
    throw "Decoded audio is entirely silent: $Path"
}

if ($LibraryOnly) { return }

Write-Host 'HPVR installer revision 3: automatic FFmpeg + Android Platform Tools.'

if ($PromptForGamePath -and [string]::IsNullOrWhiteSpace($GamePath)) {
    $GamePath = Read-Host 'Folder of your installed US PC game'
}
if ([string]::IsNullOrWhiteSpace($GamePath)) {
    throw 'Pass -GamePath with the folder of your own installed US PC game, for example -GamePath "C:\Program Files\HP".'
}
$bundleRoot = Get-HpvrFullPath $PSScriptRoot
$ownedRoot = Get-HpvrFullPath $GamePath
Assert-HpvrNoLinks $ownedRoot
if (-not (Test-Path -LiteralPath $ownedRoot -PathType Container)) { throw "GamePath is not a folder: $ownedRoot" }
if (Test-HpvrWithin $bundleRoot $ownedRoot) { throw 'Keep the release folder outside the original game installation.' }
$release = Read-HpvrReleaseManifest $bundleRoot
$toolsRoot = Join-Path $bundleRoot 'tools'
$frontendProbe = Join-Path $toolsRoot 'hpvr_quest_frontend_probe.exe'
$soundProbe = Join-Path $toolsRoot 'hpvr_hp1_sound_probe.exe'
$graphProbe = Join-Path $toolsRoot 'hpvr_hp1_package_graph.exe'
$sceneProbe = Join-Path $toolsRoot 'hpvr_quest_intro_probe.exe'

$ffmpegCandidates = @()
if ($env:ProgramFiles) { $ffmpegCandidates += Join-Path $env:ProgramFiles 'ffmpeg\bin\ffmpeg.exe' }
$ffmpeg = Get-HpvrFfmpeg $FfmpegPath $ffmpegCandidates -NoDownload:$NoToolDownload

# Validate the device before doing potentially lengthy conversion, except for explicit offline preparation.
$adb = $null
$deviceArgs = @()
if (-not $PrepareOnly) {
    $adbCandidates = @()
    if ($env:ANDROID_SDK_ROOT) { $adbCandidates += Join-Path $env:ANDROID_SDK_ROOT 'platform-tools\adb.exe' }
    if ($env:ANDROID_HOME) { $adbCandidates += Join-Path $env:ANDROID_HOME 'platform-tools\adb.exe' }
    if ($env:LOCALAPPDATA) { $adbCandidates += Join-Path $env:LOCALAPPDATA 'Android\Sdk\platform-tools\adb.exe' }
    $adb = Get-HpvrAdb $AdbPath $adbCandidates -NoDownload:$NoToolDownload
    $deviceOutput = @(& $adb devices)
    if ($LASTEXITCODE -ne 0) { throw 'ADB device enumeration failed.' }
    $devices = @($deviceOutput | Where-Object { $_ -match '^([^\s]+)\s+device\s*$' } | ForEach-Object { ($_ -split '\s+')[0] })
    if ([string]::IsNullOrWhiteSpace($DeviceSerial)) {
        if ($devices.Count -ne 1) { throw "Expected one authorized headset, found $($devices.Count). Enable developer mode and accept USB debugging, or pass -DeviceSerial." }
        $DeviceSerial = $devices[0]
    }
    if ($devices -notcontains $DeviceSerial -or $DeviceSerial -notmatch '^[A-Za-z0-9._:-]+$') { throw 'The selected headset is not connected and authorized.' }
    $deviceArgs = @('-s', $DeviceSerial)
}

if ([string]::IsNullOrWhiteSpace($WorkRoot)) {
    if (-not $env:LOCALAPPDATA) { throw 'LOCALAPPDATA is unavailable. Supply -WorkRoot outside the release and game directories.' }
    $WorkRoot = Join-Path $env:LOCALAPPDATA 'HPVR\PrivateData'
}
$privateRoot = Get-HpvrFullPath $WorkRoot
Assert-HpvrNoLinks $privateRoot
if ((Test-HpvrWithin $privateRoot $bundleRoot) -or (Test-HpvrWithin $privateRoot $ownedRoot)) {
    throw 'Private working data must be outside both the release folder and the original game installation.'
}
$privateRun = Join-Path $privateRoot ('import-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $privateRun | Out-Null
$stagedGame = Join-Path $privateRun 'HP'
$audioCache = Join-Path $stagedGame 'Cache\Audio'
$encodedRoot = Join-Path $privateRun 'encoded-private'
New-Item -ItemType Directory -Path $audioCache, $encodedRoot | Out-Null
Write-Host "Private working data: $privateRun"
Write-Host 'Original installation is read-only. No proprietary data will be added to the release directory.'

# Resolve only the first-level dependency closure, then add explicitly loaded menu/audio packages.
$map = Join-Path $ownedRoot 'Maps\Lev_Tut1.unr'
$null = Get-HpvrOwnedInput $ownedRoot $map
$graphOutput = @(Invoke-HpvrChecked $graphProbe @($ownedRoot, $map) 'Owned package dependency scan')
$graphOutput | Set-Content -LiteralPath (Join-Path $privateRun 'dependency-report.txt') -Encoding UTF8
$ownedInputs = @{}
foreach ($line in $graphOutput) {
    if ($line -match '^package=\S+ kind=data path=(.+) version=\d+ direct_dependencies=\d+ native_companion=[01]$') {
        $inputFile = Get-HpvrOwnedInput $ownedRoot $Matches[1]
        $ownedInputs[$inputFile.Relative] = $inputFile
    }
}
if ($ownedInputs.Count -lt 3) { throw 'The owned package dependency scan did not produce a valid data set.' }
foreach ($relative in @(
    'system/HPBase.u', 'system/HarryPotter.u', 'system/HPMenu.u', 'system/HPParticle.u', 'system/HProps.u', 'system/HPSounds.u',
    'system/hpmenu.int', 'system/hpdialog.int', 'Textures/MenuArt.utx', 'Textures/StoryBookTest.utx',
    'Sounds/AllDialog.uax', 'Sounds/Magic_sfx.uax', 'Sounds/Ambient.uax',
    'Music/JS_HP_Title_Screen_v2.umx', 'Music/JS_StoryBook_v2_mx.umx',
    'Music/JS_Opening_Castle_Fly_Through_mx.umx', 'Music/happy_hogwarts_mxlp1.umx')) {
    $inputFile = Get-HpvrOwnedInput $ownedRoot (Join-Path $ownedRoot $relative)
    $ownedInputs[$inputFile.Relative] = $inputFile
}

$privateManifest = [Collections.Generic.List[object]]::new()
foreach ($entry in ($ownedInputs.Values | Sort-Object Relative)) {
    $target = Join-Path $stagedGame $entry.Relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null
    $before = (Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $entry.Source -Destination $target
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $before -or
        (Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash -ne $before) {
        throw "The owned file changed or did not copy correctly: $($entry.Relative)"
    }
    $privateManifest.Add([pscustomobject]@{ path = $entry.Relative; sha256 = $before; bytes = (Get-Item -LiteralPath $target).Length })
}
Write-Host "Staged $($ownedInputs.Count) required game-data files; no EXE, DLL, user config, or save was copied."

# This probe enumerates the same FrontAssets sources used by the shipping runtime:
# opening story, four music streams, quest speech, NPC bump lines, lesson speech and pickups.
Invoke-HpvrChecked $frontendProbe @($stagedGame, $encodedRoot) 'Owned frontend/audio extraction' |
    Set-Content -LiteralPath (Join-Path $privateRun 'frontend-report.txt') -Encoding UTF8
$plan = @{}
foreach ($line in (Get-Content -LiteralPath (Join-Path $encodedRoot 'audio-plan.tsv'))) {
    if ($line -notmatch '^([A-Za-z0-9_]+\.[0-9a-f]{8}(\.stereo)?\.s16)\t([12])$') { throw 'Invalid frontend audio conversion plan.' }
    $key = $Matches[1]
    $channels = [int]$Matches[3]
    if (($key.Contains('.stereo.')) -ne ($channels -eq 2)) { throw "Inconsistent PCM channel plan: $key" }
    if ($plan.ContainsKey($key) -and $plan[$key] -ne $channels) { throw "Conflicting audio plan: $key" }
    $plan[$key] = $channels
}
if ($plan.Count -lt 20) { throw 'The full story/lesson audio plan is incomplete.' }

$extraClips = @(
    @('AllDialog.uax', '111DumbledoreInfo1'), @('AllDialog.uax', '111DumbledoreInfo2'),
    @('AllDialog.uax', '111DumbledoreInfo3'), @('AllDialog.uax', '111DumbledoreInfo4'),
    @('AllDialog.uax', 'Dumbledore_01'), @('Magic_sfx.uax', 'spell_tracing_loop'),
    @('Magic_sfx.uax', 'wand_ready_loop'), @('Magic_sfx.uax', 'spell_cast'), @('Magic_sfx.uax', 'flipendo_no')
)
foreach ($clip in $extraClips) {
    $temporaryMpeg = Join-Path $encodedRoot ($clip[1] + '.extra.mp2')
    $report = @(Invoke-HpvrChecked $soundProbe @((Join-Path $stagedGame ('Sounds/' + $clip[0])), '--export-mpeg', $clip[1], $temporaryMpeg) 'Owned intro/wand audio extraction')
    $names = @($report | Where-Object { $_ -match '^cache_name=[A-Za-z0-9_]+\.[0-9a-f]{8}\.s16$' })
    if ($names.Count -ne 1) { throw "Missing source fingerprint for $($clip[1])" }
    $key = $names[0].Substring(11)
    $encoded = Join-Path $encodedRoot ($key + '.mp2')
    if (Test-Path -LiteralPath $encoded) {
        if ((Get-FileHash -LiteralPath $encoded).Hash -ne (Get-FileHash -LiteralPath $temporaryMpeg).Hash) { throw "Conflicting encoded source: $key" }
    } else { Copy-Item -LiteralPath $temporaryMpeg -Destination $encoded }
    $plan[$key] = 1
}

$index = 0
foreach ($key in ($plan.Keys | Sort-Object)) {
    ++$index
    Write-Progress -Activity 'Preparing original music and speech' -Status "$index / $($plan.Count): $key" -PercentComplete (100.0 * $index / $plan.Count)
    $pcm = Join-Path $audioCache $key
    Invoke-HpvrChecked $ffmpeg @('-nostdin', '-hide_banner', '-loglevel', 'error', '-y', '-i',
        (Join-Path $encodedRoot ($key + '.mp2')), '-f', 's16le', '-acodec', 'pcm_s16le',
        '-ac', [string]$plan[$key], '-ar', '48000', $pcm) 'Audio decoding'
    Assert-HpvrPcm $pcm $plan[$key]
    $privateManifest.Add([pscustomobject]@{ path = 'Cache/Audio/' + $key; sha256 = (Get-FileHash -LiteralPath $pcm -Algorithm SHA256).Hash; bytes = (Get-Item -LiteralPath $pcm).Length })
}
Write-Progress -Activity 'Preparing original music and speech' -Completed

Write-Host 'Checking the complete staged first-level scene and audio. This is offline validation, not a game launch.'
Invoke-HpvrChecked $sceneProbe @($stagedGame, $audioCache) 'Staged scene/audio validation' |
    Set-Content -LiteralPath (Join-Path $privateRun 'scene-report.txt') -Encoding UTF8
$privateManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $privateRun 'PRIVATE-owned-data-manifest.json') -Encoding UTF8
if ($PrepareOnly) {
    Write-Host "PREPARE=PASS files=$($privateManifest.Count) audio_clips=$($plan.Count) private_path=$privateRun"
    Write-Host 'APK_INSTALL=NOT_PERFORMED DATA_PUSH=NOT_PERFORMED APP_LAUNCH=NOT_PERFORMED'
    return
}

# Hashes are corruption checks, not a substitute for obtaining the kit from a trusted author.
# Android verifies the signed APK and rejects a different installed signing certificate.
$apk = Join-Path $bundleRoot $release.apk
$apkEntry = @($release.files | Where-Object { $_.path -eq $release.apk })[0]
if ((Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash -ne $apkEntry.sha256) { throw 'APK changed during preparation.' }
Write-Host 'Installing the signed APK in place. Existing saves/settings are not intentionally modified.'
& $adb @deviceArgs install --no-incremental -r $apk
if ($LASTEXITCODE -ne 0) {
    throw 'APK installation failed. If Android reports UPDATE_INCOMPATIBLE, an older build uses a different signing key. Nothing is uninstalled or erased automatically. Contact the author about migration before removing the old app.'
}
$installedPaths = @(& $adb @deviceArgs shell pm path $release.packageName)
if ($LASTEXITCODE -ne 0 -or $installedPaths.Count -ne 1 -or $installedPaths[0] -notmatch '^package:(/data/app/[A-Za-z0-9_./~+=-]+/base\.apk)\s*$') { throw 'Could not resolve the installed standalone base APK.' }
$installedApk = $Matches[1]
$installedHash = (@(& $adb @deviceArgs shell "sha256sum '$installedApk'") -join "`n")
if ($LASTEXITCODE -ne 0 -or $installedHash -notmatch '^([0-9a-fA-F]{64})\s' -or $Matches[1] -ne $apkEntry.sha256) { throw 'Installed APK hash verification failed.' }
Invoke-HpvrChecked $adb ($deviceArgs + @('shell', 'am', 'force-stop', $release.packageName)) 'Stopping this app before data import'
$remoteRoot = '/sdcard/Android/data/io.github.hpvr.quest/files/HP'
Invoke-HpvrChecked $adb ($deviceArgs + @('shell', "mkdir -p '$remoteRoot'")) 'Creating app-owned external data folder'
foreach ($folder in @('system', 'Maps', 'Textures', 'Sounds', 'Music', 'Cache/Audio')) {
    Invoke-HpvrChecked $adb ($deviceArgs + @('shell', "mkdir -p '$remoteRoot/$folder'")) 'Creating game-data subfolder'
}
$index = 0
foreach ($entry in $privateManifest) {
    ++$index
    Assert-HpvrSafeRelative $entry.path
    $local = Join-Path $stagedGame $entry.path
    if ((Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Prepared data changed: $($entry.path)" }
    $remote = $remoteRoot + '/' + $entry.path
    Write-Progress -Activity 'Importing your game data to Quest' -Status "$index / $($privateManifest.Count): $($entry.path)" -PercentComplete (100.0 * $index / $privateManifest.Count)
    Invoke-HpvrChecked $adb ($deviceArgs + @('push', '--sync', $local, $remote)) 'Owned-data transfer'
    $hashOutput = (@(& $adb @deviceArgs shell "sha256sum '$remote'") -join "`n")
    if ($LASTEXITCODE -ne 0 -or $hashOutput -notmatch '^([0-9a-fA-F]{64})\s' -or $Matches[1] -ne $entry.sha256) {
        throw "Headset data verification failed: $($entry.path). Re-run the installer to retry. Saves have not been touched."
    }
}
Write-Progress -Activity 'Importing your game data to Quest' -Completed
Write-Host "INSTALL=PASS DATA_IMPORT=PASS files=$($privateManifest.Count) audio_clips=$($plan.Count) device=$DeviceSerial"
Write-Host 'APP_LAUNCH=NOT_PERFORMED. Start Harry Potter VR yourself from Unknown Sources on the headset.'
Write-Host "The private local import folder can be removed manually after a successful import: $privateRun"
Write-Host 'Do not upload or redistribute the HP folder, encoded-private folder, audio cache, or private manifest.'
