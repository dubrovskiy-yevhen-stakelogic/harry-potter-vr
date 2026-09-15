#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ApkPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$HostBuildDirectory = 'build/quest-host-tests'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$hpvrRepository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\', '/')
$hpvrArtifacts = Join-Path $hpvrRepository 'artifacts'

function Resolve-PlayerPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'Empty paths are not accepted.' }
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $hpvrRepository $Path }
    return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}
function Test-PlayerWithin([string]$Child, [string]$Root) {
    return $Child.StartsWith($Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}
function Assert-PlayerNoLinks([string]$Path) {
    $current = $Path
    while (-not [string]::IsNullOrWhiteSpace($current)) {
        if (Test-Path -LiteralPath $current) {
            if (((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing symbolic link/junction: $current"
            }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}
function Assert-PlayerText([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -eq 0 -or $bytes.Length -gt 8388608 -or $bytes -contains 0) { throw "Invalid release text file: $Path" }
    $utf8 = New-Object Text.UTF8Encoding($false, $true)
    $null = $utf8.GetString($bytes)
}
function Assert-PlayerNoVoiceDiagnostics($Archive, [string]$Version) {
    # Release metadata and an APK filename are not proof that recording code is
    # absent. Inspect the exact payload even for an explicitly supplied APK.
    if ([string]::IsNullOrWhiteSpace($Version) -or $Version -match '(?i)voice[-_]?diag') {
        throw 'Voice recording diagnostic APKs must never be packaged for players.'
    }
    foreach ($name in @('AndroidManifest.xml', 'lib/arm64-v8a/libhpvr_quest.so')) {
        $entry = $Archive.GetEntry($name)
        $limit = $(if ($name -eq 'AndroidManifest.xml') { 8388608L } else { 536870912L })
        if ($null -eq $entry -or $entry.Length -le 0 -or $entry.Length -gt $limit) {
            throw "Cannot verify recording-code absence in APK component: $name"
        }
        $needles = @('HPVR_LOCAL_VOICE_RECORDING_DIAGNOSTIC', 'HPVR_LOCAL_VOICE_RECORDING_30S')
        if ($name -eq 'AndroidManifest.xml') {
            # Android binary XML string pools may use UTF-8 or UTF-16LE.
            $needles += @('-voice-diag', 'voiceDiagnostic',
                [Text.Encoding]::ASCII.GetString([Text.Encoding]::Unicode.GetBytes('-voice-diag')),
                [Text.Encoding]::ASCII.GetString([Text.Encoding]::Unicode.GetBytes('voiceDiagnostic')))
        }
        $overlap = ($needles | ForEach-Object { $_.Length } | Measure-Object -Maximum).Maximum - 1
        $buffer = New-Object byte[] 65536
        $tail = ''
        $total = 0L
        $stream = $entry.Open()
        try {
            while (($count = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                $total += $count
                if ($total -gt $entry.Length -or $total -gt $limit) { throw "Unbounded APK component: $name" }
                $window = $tail + [Text.Encoding]::ASCII.GetString($buffer, 0, $count)
                foreach ($needle in $needles) {
                    if ($window.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                        throw "Voice recording diagnostic payload is forbidden in player packages: $name"
                    }
                }
                $tail = $window.Substring([Math]::Max(0, $window.Length - $overlap))
            }
            if ($total -ne $entry.Length) { throw "Incomplete APK component verification: $name" }
        } finally { $stream.Dispose() }
    }
}

$hpvrApk = Resolve-PlayerPath $ApkPath
$hpvrOutput = Resolve-PlayerPath $OutputDirectory
$hpvrHostBuild = Resolve-PlayerPath $HostBuildDirectory
$hpvrZip = $hpvrOutput + '.zip'
foreach ($path in @($hpvrApk, $hpvrOutput, $hpvrHostBuild, $hpvrZip)) { Assert-PlayerNoLinks $path }
if (-not (Test-PlayerWithin $hpvrApk $hpvrArtifacts) -or -not (Test-PlayerWithin $hpvrOutput $hpvrArtifacts) -or
    -not (Test-PlayerWithin $hpvrHostBuild $hpvrRepository)) {
    throw 'Release APK/output must be inside this repository artifacts/, and host build inside this repository.'
}
if ((Test-Path -LiteralPath $hpvrOutput) -or (Test-Path -LiteralPath $hpvrZip)) {
    throw 'Player directory or ZIP already exists. Use a new -OutputDirectory; nothing is overwritten or deleted.'
}
if (-not (Test-Path -LiteralPath $hpvrApk -PathType Leaf)) { throw "Build and verify the release APK first: $hpvrApk" }
$hpvrReleaseRoot = [IO.Path]::GetDirectoryName($hpvrApk)
$hpvrMetadataPath = Join-Path $hpvrReleaseRoot 'RELEASE-METADATA.json'
Assert-PlayerNoLinks $hpvrMetadataPath
$hpvrMetadata = Get-Content -LiteralPath $hpvrMetadataPath -Raw | ConvertFrom-Json
$hpvrApkHash = (Get-FileHash -LiteralPath $hpvrApk -Algorithm SHA256).Hash
if ($hpvrMetadata.package -ne 'io.github.hpvr.quest' -or $hpvrMetadata.buildType -ne 'Release' -or
    $hpvrMetadata.debuggable -ne $false -or $hpvrMetadata.abi -ne 'arm64-v8a' -or
    $hpvrMetadata.gameAssetsIncluded -ne $false -or $hpvrMetadata.apkSha256 -ne $hpvrApkHash -or
    $hpvrMetadata.certificateSha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'APK does not match the verified asset-free ARM64 release metadata. Rebuild/verify the release first.'
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
. (Join-Path $hpvrRepository 'tools/voice/VOICE-APK-PAYLOAD.ps1')
$hpvrApkArchive = [IO.Compression.ZipFile]::OpenRead($hpvrApk)
try {
    Assert-PlayerNoVoiceDiagnostics $hpvrApkArchive $hpvrMetadata.version
    Assert-HpvrApkPayload $hpvrApkArchive ($hpvrMetadata.versionCode -ge 44) (Join-Path $hpvrRepository 'tools/voice/VOICE-ASSETS.psd1')
    $seenApkNames = @{}
    foreach ($entry in $hpvrApkArchive.Entries) {
        $name = $entry.FullName
        if ($seenApkNames.ContainsKey($name)) { throw "Duplicate APK entry: $name" }
        $seenApkNames[$name] = $true
        if ($name -match '(?i)\.(unr|utx|uax|umx|u|s16|hpvc|mp2|wav|mp3|ogg|jks|keystore|pem|p12)$' -or
            $name -match '(^|/)\.\.(/|$)' -or $name.Contains('\')) {
            throw "Game data, secret, or unsafe path found in APK: $name"
        }
        if ($name.StartsWith('lib/') -and $name -notin @('lib/arm64-v8a/libhpvr_quest.so', 'lib/arm64-v8a/libopenxr_loader.so')) {
            throw "Unexpected APK native binary: $name"
        }
    }
    foreach ($required in @('AndroidManifest.xml', 'lib/arm64-v8a/libhpvr_quest.so', 'lib/arm64-v8a/libopenxr_loader.so')) {
        if (-not $seenApkNames.ContainsKey($required)) { throw "Missing APK component: $required" }
    }
} finally { $hpvrApkArchive.Dispose() }

# Deliberately explicit: never collect a directory of arbitrary source or build files.
$hpvrCopies = [Collections.Generic.List[object]]::new()
function Add-PlayerInput([string]$Source, [string]$Destination, [string]$Kind) {
    if ($Destination -notmatch '^[A-Za-z0-9_./-]+$' -or $Destination.Contains('..')) { throw 'Invalid allowlist destination.' }
    $sourcePath = Resolve-PlayerPath $Source
    Assert-PlayerNoLinks $sourcePath
    if (-not (Test-PlayerWithin $sourcePath $hpvrRepository) -or -not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Missing/nonlocal allowlisted input: $sourcePath"
    }
    if ($Kind -eq 'text') { Assert-PlayerText $sourcePath }
    elseif ($Kind -eq 'tool') {
        $stream = [IO.File]::OpenRead($sourcePath)
        try { if ($stream.ReadByte() -ne 77 -or $stream.ReadByte() -ne 90) { throw "Expected our built Windows executable: $sourcePath" } }
        finally { $stream.Dispose() }
    }
    $inputHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    if ($Kind -eq 'apk' -and $inputHash -ne $hpvrApkHash) {
        throw 'APK changed after metadata/payload verification. Refusing to package an unverified replacement.'
    }
    $hpvrCopies.Add([pscustomobject]@{ source = $sourcePath; path = $Destination; kind = $Kind; sha256 = $inputHash })
}
Add-PlayerInput $hpvrApk 'HPVR-Quest-Demo.apk' 'apk'
foreach ($name in @('INSTALL-HPVR.ps1', 'INSTALL-HPVR.cmd', 'PLAYER-INSTALL.md')) {
    Add-PlayerInput ('tools/release/' + $name) $name 'text'
}
Add-PlayerInput 'README.md' 'README.md' 'player-readme'
Add-PlayerInput 'README.md' 'FEATURES.md' 'player-readme'
Add-PlayerInput 'docs/THIRD-PARTY-NOTICES.md' 'THIRD-PARTY-NOTICES.md' 'text'
foreach ($name in @('hpvr_hp1_package_graph.exe', 'hpvr_hp1_sound_probe.exe')) {
    Add-PlayerInput (Join-Path $hpvrHostBuild ('src/wand/Release/' + $name)) ('tools/' + $name) 'tool'
}
foreach ($name in @('hpvr_quest_frontend_probe.exe', 'hpvr_quest_intro_probe.exe')) {
    Add-PlayerInput (Join-Path $hpvrHostBuild ('src/quest/Release/' + $name)) ('tools/' + $name) 'tool'
}
$hpvrPreparedSceneVersion = 0
if ($hpvrMetadata.versionCode -ge 43) {
    Add-PlayerInput (Join-Path $hpvrHostBuild 'src/quest/Release/hpvr_quest_prepare_assets.exe') 'tools/hpvr_quest_prepare_assets.exe' 'tool'
    $hpvrPreparedSceneVersion = 1
} else {
    Write-Warning 'This APK predates prepared-scene support. Packaging the compatible packages/audio-only installer path; no scene preparation tool is included.'
}

# These are the exact audited notices emitted by BUILD-QUEST-RELEASE.ps1. A new
# dependency requires an explicit audit/allowlist update, not a broad recursive copy.
$hpvrNoticeNames = @('NDK-27.2.12479018-NOTICE.toolchain.txt', 'NDK-27.2.12479018-NOTICE.txt',
    'OpenXR-1.1.43-LICENSE.txt', 'THIRD-PARTY-NOTICES.md')
if ($hpvrMetadata.versionCode -ge 47) {
    $hpvrVoiceManifest = Import-PowerShellDataFile -LiteralPath (Join-Path $hpvrRepository 'tools/voice/VOICE-ASSETS.psd1')
    if ($hpvrVoiceManifest.Count -ne 24) { throw 'Unexpected neural voice asset inventory.' }
    $hpvrNoticeNames += @($hpvrVoiceManifest.Keys | Where-Object { $_ -match '^(LICENSE-|NOTICE-|EIGEN-|MODEL-README)' } |
        ForEach-Object { "Voice-$_" })
} elseif ($hpvrMetadata.versionCode -ge 44) {
    $hpvrNoticeNames += @('PocketSphinx-5.0.4-LICENSE.txt', 'PocketSphinx-en-us-model-README.txt')
}
$hpvrNoticeRoot = Join-Path $hpvrReleaseRoot 'THIRD-PARTY'
Assert-PlayerNoLinks $hpvrNoticeRoot
$hpvrNoticeFiles = @(Get-ChildItem -LiteralPath $hpvrNoticeRoot -Force)
if ($hpvrNoticeFiles.Count -ne $hpvrNoticeNames.Count) { throw 'Unexpected third-party notice inventory; audit it before packaging.' }
foreach ($notice in $hpvrNoticeFiles) {
    if ($notice.PSIsContainer -or $notice.Name -notin $hpvrNoticeNames -or $notice.Extension -notin @('.txt', '.md')) {
        throw "Unaudited third-party file: $($notice.Name)"
    }
    Add-PlayerInput $notice.FullName ('THIRD-PARTY/' + $notice.Name) 'text'
}

New-Item -ItemType Directory -Path $hpvrOutput | Out-Null
$hpvrFiles = [Collections.Generic.List[object]]::new()
foreach ($copy in $hpvrCopies) {
    $destination = Join-Path $hpvrOutput $copy.path
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
    $expectedCopyHash = $copy.sha256
    if ($copy.kind -eq 'player-readme') {
        Assert-PlayerText $copy.source
        $playerReadme = [IO.File]::ReadAllText($copy.source)
        $developmentHeading = $playerReadme.IndexOf('<!-- player-readme-end -->', [StringComparison]::Ordinal)
        if ($developmentHeading -lt 0) { throw 'README player/developer section boundary is missing.' }
        $playerReadme = $playerReadme.Substring(0, $developmentHeading).TrimEnd()
        $playerReadme = [regex]::Replace($playerReadme, '(?s)## Want to play\?.*?(?=## Included levels)',
            "## Getting started`n`nRun ``INSTALL-HPVR.cmd`` and select your original PC game folder.`n`n")
        $playerReadme = $playerReadme.Replace('(tools/release/PLAYER-INSTALL.md)', '(PLAYER-INSTALL.md)')
        $playerReadme += "`n`nDiscord: [HPVR](https://discord.com/channels/747967102895390741/1547254536203407390).`n"
        [IO.File]::WriteAllText($destination, $playerReadme, [Text.UTF8Encoding]::new($false))
        $expectedCopyHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    } else {
        Copy-Item -LiteralPath $copy.source -Destination $destination
    }
    if ((Get-FileHash -LiteralPath $copy.source -Algorithm SHA256).Hash -ne $copy.sha256 -or
        (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $expectedCopyHash) {
        throw "Input changed or copy failed: $($copy.path). Partial player folder is retained for inspection."
    }
    $hpvrFiles.Add([pscustomobject]@{ path = $copy.path; sha256 = $expectedCopyHash; bytes = (Get-Item -LiteralPath $destination).Length })
}
$hpvrManifest = [ordered]@{
    schema = 1
    packageName = 'io.github.hpvr.quest'
    apk = 'HPVR-Quest-Demo.apk'
    version = $hpvrMetadata.version
    versionCode = $hpvrMetadata.versionCode
    certificateSha256 = $hpvrMetadata.certificateSha256
    gameAssetsIncluded = $false
    # Explicit release scope: installers do not guess from files beside them.
    # Keep rebuilding historical one-map APKs possible with explicit paths.
    mapIds = @(if ($hpvrMetadata.versionCode -ge 71) { 0; 1; 2; 3 } elseif ($hpvrMetadata.versionCode -ge 57) { 0; 1; 2 } elseif ($hpvrMetadata.versionCode -ge 38) { 0; 1 } else { 0 })
    files = @($hpvrFiles.ToArray())
}
if ($hpvrPreparedSceneVersion -gt 0) { $hpvrManifest.preparedSceneVersion = $hpvrPreparedSceneVersion }
$hpvrManifestPath = Join-Path $hpvrOutput 'release-manifest.json'
$hpvrManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $hpvrManifestPath -Encoding UTF8

# Reuse the shipping installer's independent manifest checks before archiving.
. (Join-Path $hpvrOutput 'INSTALL-HPVR.ps1') -LibraryOnly
$null = Read-HpvrReleaseManifest $hpvrOutput
$hpvrAllowed = @{}
foreach ($entry in $hpvrFiles) { $hpvrAllowed[$entry.path] = $entry.sha256 }
$hpvrAllowed['release-manifest.json'] = (Get-FileHash -LiteralPath $hpvrManifestPath -Algorithm SHA256).Hash
$hpvrActualFiles = @(Get-ChildItem -LiteralPath $hpvrOutput -Recurse -Force -File)
if ($hpvrActualFiles.Count -ne $hpvrAllowed.Count) { throw 'Player folder contains unexpected files.' }
foreach ($entry in $hpvrActualFiles) {
    Assert-PlayerNoLinks $entry.FullName
    $relative = $entry.FullName.Substring($hpvrOutput.Length + 1).Replace('\', '/')
    if (-not $hpvrAllowed.ContainsKey($relative) -or (Get-FileHash -LiteralPath $entry.FullName -Algorithm SHA256).Hash -ne $hpvrAllowed[$relative]) {
        throw "Non-allowlisted or changed player file: $relative"
    }
}

$hpvrTemporaryZip = $hpvrZip + '.partial-' + [Guid]::NewGuid().ToString('N')
if (-not (Test-PlayerWithin $hpvrTemporaryZip $hpvrArtifacts)) { throw 'ZIP must remain inside artifacts/.' }
# .NET Framework's CreateFromDirectory emits Windows backslashes on some hosts.
# Write each allowlisted entry explicitly with portable forward-slash ZIP names.
$hpvrZipFile = [IO.File]::Open($hpvrTemporaryZip, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
try {
    $hpvrWritingArchive = New-Object IO.Compression.ZipArchive($hpvrZipFile, [IO.Compression.ZipArchiveMode]::Create, $true)
    try {
        foreach ($relative in ($hpvrAllowed.Keys | Sort-Object)) {
            $entry = $hpvrWritingArchive.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
            $destinationStream = $entry.Open()
            $sourceStream = [IO.File]::OpenRead((Join-Path $hpvrOutput $relative))
            try { $sourceStream.CopyTo($destinationStream) }
            finally { $sourceStream.Dispose(); $destinationStream.Dispose() }
        }
    } finally { $hpvrWritingArchive.Dispose() }
} finally { $hpvrZipFile.Dispose() }
$hpvrArchive = [IO.Compression.ZipFile]::OpenRead($hpvrTemporaryZip)
try {
    if ($hpvrArchive.Entries.Count -ne $hpvrAllowed.Count) { throw 'ZIP inventory does not match the allowlist.' }
    $hpvrZipSeen = @{}
    foreach ($entry in $hpvrArchive.Entries) {
        if (-not $hpvrAllowed.ContainsKey($entry.FullName) -or $hpvrZipSeen.ContainsKey($entry.FullName)) { throw "Unexpected ZIP entry: $($entry.FullName)" }
        $hpvrZipSeen[$entry.FullName] = $true
        $hash = [Security.Cryptography.SHA256]::Create()
        $stream = $entry.Open()
        try { $entryHash = [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose(); $hash.Dispose() }
        if ($entryHash -ne $hpvrAllowed[$entry.FullName]) { throw "ZIP hash mismatch: $($entry.FullName)" }
    }
} finally { $hpvrArchive.Dispose() }
# File.Move never overwrites an existing final archive, even after a concurrent creation.
[IO.File]::Move($hpvrTemporaryZip, $hpvrZip)
$hpvrZipHash = (Get-FileHash -LiteralPath $hpvrZip -Algorithm SHA256).Hash
Write-Output "PLAYER_PACKAGE=PASS files=$($hpvrAllowed.Count) game_assets=0 voice_model=$([int]($hpvrMetadata.versionCode -ge 44)) extra_third_party_tools=0 keys=0"
Write-Output "DIRECTORY=$hpvrOutput"
Write-Output "ZIP=$hpvrZip"
Write-Output "ZIP_SHA256=$hpvrZipHash"
Write-Output "APK_SHA256=$hpvrApkHash"
Write-Output 'OWNED_DATA_PREPARATION=NOT_PERFORMED APK_INSTALL=NOT_PERFORMED APP_LAUNCH=NOT_PERFORMED'
Write-Output 'Before shipping: run the bundled INSTALL-HPVR.ps1 -PrepareOnly against an owned US PC installation, with -WorkRoot outside this player folder.'
