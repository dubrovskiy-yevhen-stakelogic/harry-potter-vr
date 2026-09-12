[CmdletBinding()]
param(
    [string]$DestinationPath,
    [switch]$AuditOnly,
    [switch]$CreateArchive
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repositoryRoot = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\', '/')
$repositoryPrefix = $repositoryRoot + [IO.Path]::DirectorySeparatorChar
if ([string]::IsNullOrWhiteSpace($DestinationPath)) {
    $DestinationPath = Join-Path (Split-Path -Parent $repositoryRoot) ((Split-Path -Leaf $repositoryRoot) + '-source-kit')
}
$destination = [IO.Path]::GetFullPath($DestinationPath).TrimEnd('\', '/')
$destinationParent = Split-Path -Parent $destination
$archivePath = $destination + '.zip'

# This exports current files, not HEAD: production changes can be uncommitted
# or untracked. It never traverses the private local/build/retail data trees.
$rootFiles = @(
    '.gitattributes', '.gitignore', 'CMakeLists.txt', 'README.md',
    'SOURCE-KIT-README.md', 'EXPORT-SOURCE-KIT.ps1', 'BUILD-QUEST-DEBUG.ps1',
    'PACKAGE-QUEST-DEBUG.ps1', 'VERIFY-QUEST-APK.ps1', 'IMPORT-QUEST-DATA.ps1',
    'BUILD-QUEST-RELEASE.ps1', 'PACKAGE-QUEST-PLAYER.ps1',
    'PREPARE-QUEST-AUDIO.ps1', 'PREPARE-QUEST-FRONTEND.ps1', 'RUN-LATEST-VR.cmd',
    'PREPARE-QUEST-CHALLENGE.ps1', 'PREPARE-QUEST-BROOM.ps1',
    'tools/test-quest-broom-preparation.ps1',
    'android/build.gradle', 'android/settings.gradle', 'android/gradle.properties',
    'android/app/build.gradle', 'tools/verify-baseline.ps1', 'tools/test-source-kit-export.ps1',
    'tools/release/INSTALL-HPVR.ps1', 'tools/release/INSTALL-HPVR.cmd',
    'tools/release/PLAYER-INSTALL.md', 'tools/release/TEST-PLAYER-INSTALL.ps1',
    'tools/release/TEST-ADB-BOOTSTRAP.ps1', 'tools/release/TEST-FFMPEG-BOOTSTRAP.ps1',
    'cmake/HPVRVoice.cmake', 'tools/voice/CMakeLists.txt',
    'tools/voice/FETCH-VOICE-DEPENDENCIES.ps1', 'tools/voice/BUILD-VOICE-CHECKS.ps1',
    'tools/voice/TEST-VOICE-APK-PAYLOAD.ps1', 'tools/voice/VOICE-ASSETS.psd1',
    'tools/voice/VOICE-APK-PAYLOAD.ps1',
    'tools/voice/BUILD-NEURAL-VOICE-RUNTIME.ps1', 'tools/voice/NEURAL-RUNTIME-NOTICES.psd1',
    'tools/voice/TEST-NEURAL-VOICE-RUNTIME.ps1',
    'tools/voice/flipendo.keywords', 'tools/voice/voice_keyword_probe.cpp', 'tools/voice/README.md',
    'docs/architecture.md', 'docs/wand-gesture-contract.md',
    'docs/RELEASE-BUILD.md', 'docs/THIRD-PARTY-NOTICES.md', 'docs/VOICE-HOTFIX-0.1.2.1.md',
    'docs/CLASSIC-CASTING-AND-CHALLENGE.md', 'docs/FLIPENDO-CHALLENGE.md', 'docs/LOADING-AND-PICKUPS.md',
    'tools/xr-runtime-probe/Cargo.toml', 'tools/xr-runtime-probe/Cargo.lock',
    'tools/xr-runtime-probe/rust-toolchain.toml', 'tools/xr-runtime-probe/build.rs'
)
$optionalRootFiles = @('LICENSE', 'LICENSE.md', 'LICENSE.txt', 'COPYING', 'COPYING.md', 'COPYING.txt', 'NOTICE', 'NOTICE.md', 'NOTICE.txt')
$treeRules = @(
    @{ Path = 'src'; Extensions = @('.cpp', '.c', '.h', '.hpp', '.inl', '.cmake'); Names = @('CMakeLists.txt') },
    @{ Path = 'android/app/src/main'; Extensions = @('.cpp', '.c', '.h', '.hpp', '.inl', '.cmake', '.vert', '.frag', '.xml', '.java', '.kt'); Names = @('CMakeLists.txt') },
    @{ Path = 'tools/xr-runtime-probe/src'; Extensions = @('.rs', '.wgsl'); Names = @() }
)
$excludedDirectories = @(
    '.git', '.codex', '.agents', '.gradle', '.cxx', 'build', 'target', 'local',
    'game-data', 'asset-dumps', 'install-snapshot', 'out', 'artifacts',
    'assets', 'generated', 'captures', 'screenshots', 'SaveGames', 'saves',
    'vendor', 'third_party', 'third-party', 'node_modules'
)
$strictUtf8 = [Text.UTF8Encoding]::new($false, $true)

function Assert-NotReparsePoint([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing a link/junction in source-kit input or output: $Path"
    }
}

function Get-SourceHash([byte[]]$Bytes) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($Bytes)).Replace('-', '') }
    finally { $algorithm.Dispose() }
}

function Get-AuditedRecord([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (-not $absolute.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Source escaped the repository: $absolute"
    }
    Assert-NotReparsePoint $absolute
    $bytes = [IO.File]::ReadAllBytes($absolute)
    if ($bytes.Length -gt 16MB) { throw "Unexpectedly large source file: $absolute" }
    if ($bytes.Length -ge 4 -and (
            ($bytes[0] -eq 0xC1 -and $bytes[1] -eq 0x83 -and $bytes[2] -eq 0x2A -and $bytes[3] -eq 0x9E) -or
            ($bytes[0] -eq 0x50 -and $bytes[1] -eq 0x4B -and $bytes[2] -eq 0x03 -and $bytes[3] -eq 0x04) -or
            ($bytes[0] -eq 0x7F -and $bytes[1] -eq 0x45 -and $bytes[2] -eq 0x4C -and $bytes[3] -eq 0x46) -or
            ($bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A))) {
        throw "Binary/package signature in source-only input: $absolute"
    }
    $decoded = $strictUtf8.GetString($bytes)
    if ($decoded.IndexOf([char]0) -ge 0) { throw "NUL/binary data in source-only input: $absolute" }
    [pscustomobject]@{
        Path = $absolute.Substring($repositoryPrefix.Length).Replace('\', '/')
        Bytes = $bytes.Length
        SHA256 = Get-SourceHash $bytes
    }
}

function Get-AllowedTreeFiles([string]$Directory, $Rule) {
    Assert-NotReparsePoint $Directory
    foreach ($item in Get-ChildItem -LiteralPath $Directory -Force) {
        if ($item.PSIsContainer) {
            if ($excludedDirectories -contains $item.Name) { continue }
            Get-AllowedTreeFiles $item.FullName $Rule
        } elseif ($Rule.Extensions -contains $item.Extension.ToLowerInvariant() -or
                  $Rule.Names -contains $item.Name) {
            Get-AuditedRecord $item.FullName
        }
    }
}

function Get-SourceInventory {
    Assert-NotReparsePoint $repositoryRoot
    $records = @(
        foreach ($relative in $rootFiles) {
            $absolute = Join-Path $repositoryRoot $relative
            if (-not (Test-Path -LiteralPath $absolute -PathType Leaf)) {
                throw "Required source-kit input is missing: $relative"
            }
            $ancestor = Split-Path -Parent $absolute
            while ($ancestor.Length -ge $repositoryRoot.Length) {
                Assert-NotReparsePoint $ancestor
                if ($ancestor -eq $repositoryRoot) { break }
                $ancestor = Split-Path -Parent $ancestor
            }
            Get-AuditedRecord $absolute
        }
        foreach ($rule in $treeRules) {
            $absolute = Join-Path $repositoryRoot $rule.Path
            $ancestor = $absolute
            while ($ancestor.Length -ge $repositoryRoot.Length) {
                Assert-NotReparsePoint $ancestor
                if ($ancestor -eq $repositoryRoot) { break }
                $ancestor = Split-Path -Parent $ancestor
            }
            Get-AllowedTreeFiles $absolute $rule
        }
        foreach ($relative in $optionalRootFiles) {
            $absolute = Join-Path $repositoryRoot $relative
            if (Test-Path -LiteralPath $absolute -PathType Leaf) {
                Get-AuditedRecord $absolute
            }
        }
    )
    $records | Sort-Object Path -Unique
}

if (-not $AuditOnly) {
if ([string]::IsNullOrWhiteSpace($destinationParent) -or
    -not (Test-Path -LiteralPath $destinationParent -PathType Container)) {
    throw 'Destination must be a new folder inside an existing parent directory.'
}
if ($destination -eq $repositoryRoot -or
    $destination.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $repositoryRoot.StartsWith($destination + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Destination cannot contain, equal, or be inside the source repository.'
}
$ancestor = $destinationParent
while (-not [string]::IsNullOrWhiteSpace($ancestor)) {
    Assert-NotReparsePoint $ancestor
    $ancestor = Split-Path -Parent $ancestor
}
if (Test-Path -LiteralPath $destination) {
    throw "Destination already exists; refusing to merge or overwrite it: $destination"
}
if ($CreateArchive -and (Test-Path -LiteralPath $archivePath)) {
    throw "Source archive already exists; refusing to overwrite it: $archivePath"
}
}

$inventory = @(Get-SourceInventory)
if ($inventory.Count -lt 30) { throw 'Incomplete source inventory.' }
$bytesTotal = ($inventory | Measure-Object -Property Bytes -Sum).Sum
Write-Host "[hpvr.source-kit.audit] status=PASS files=$($inventory.Count) bytes=$bytesTotal"
Write-Host '[hpvr.source-kit.audit] content=text-source-build-config-documentation no-game-data no-binaries no-saves'
if ($AuditOnly) {
    Write-Host '[hpvr.source-kit.export] action=NONE audit-only'
    return
}

# A fresh staging directory prevents a failed copy from looking like a complete
# kit. No cleanup or overwrite of existing user files is performed on failure.
$staging = $destination + '.staging-' + [guid]::NewGuid().ToString('N')
if ((Split-Path -Parent $staging) -ne $destinationParent -or
    -not $staging.StartsWith($destination + '.staging-', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Invalid source-kit staging path.'
}
New-Item -ItemType Directory -Path $staging | Out-Null
try {
    foreach ($record in $inventory) {
        $source = Join-Path $repositoryRoot $record.Path
        $copy = Join-Path $staging $record.Path
        $copyParent = Split-Path -Parent $copy
        if (-not (Test-Path -LiteralPath $copyParent)) {
            New-Item -ItemType Directory -Path $copyParent -Force | Out-Null
        }
        Copy-Item -LiteralPath $source -Destination $copy
        if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $record.SHA256) {
            throw "Source changed while copying: $($record.Path)"
        }
    }
    $after = @(Get-SourceInventory)
    $beforeLines = @($inventory | ForEach-Object { "$($_.SHA256)  $($_.Path)" })
    $afterLines = @($after | ForEach-Object { "$($_.SHA256)  $($_.Path)" })
    if ($null -ne (Compare-Object $beforeLines $afterLines)) {
        throw 'Source files changed during export; snapshot was not finalized.'
    }
    [IO.File]::WriteAllLines((Join-Path $staging 'SOURCE-SHA256.txt'), $beforeLines, [Text.UTF8Encoding]::new($false))
    $metadata = [ordered]@{
        format = 'hpvr-source-kit-v1'
        createdUtc = [DateTime]::UtcNow.ToString('o')
        sourceFiles = $inventory.Count
        sourceBytes = $bytesTotal
        payload = 'Text source, build configuration, documentation only. No game assets, binaries, saves or private caches.'
        license = 'No project-wide redistribution license is granted by this snapshot.'
    } | ConvertTo-Json
    [IO.File]::WriteAllText((Join-Path $staging 'SOURCE-KIT.json'), $metadata + "`n", [Text.UTF8Encoding]::new($false))
    if (Test-Path -LiteralPath $destination) { throw 'Destination appeared during export; refusing to overwrite it.' }
    # Both absolute paths were checked above and are siblings in the selected
    # existing parent. Move only this newly-created staging directory.
    Move-Item -LiteralPath $staging -Destination $destination
    Write-Host "[hpvr.source-kit.export] status=PASS path=$destination files=$($inventory.Count)"
    Write-Host "[hpvr.source-kit.export] manifest=$(Join-Path $destination 'SOURCE-SHA256.txt')"
    if ($CreateArchive) {
        Add-Type -AssemblyName System.IO.Compression
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $archiveEntries = @{}
        foreach ($record in $inventory) { $archiveEntries[$record.Path] = $record.SHA256 }
        foreach ($name in @('SOURCE-SHA256.txt', 'SOURCE-KIT.json')) {
            $archiveEntries[$name] = (Get-FileHash -LiteralPath (Join-Path $destination $name) -Algorithm SHA256).Hash
        }
        $actualFiles = @(Get-ChildItem -LiteralPath $destination -Recurse -Force -File)
        if ($actualFiles.Count -ne $archiveEntries.Count) { throw 'Unexpected file added to source kit before archiving.' }
        foreach ($file in $actualFiles) {
            Assert-NotReparsePoint $file.FullName
            $relative = $file.FullName.Substring($destination.Length + 1).Replace('\', '/')
            if (-not $archiveEntries.ContainsKey($relative) -or
                (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $archiveEntries[$relative]) {
                throw "Source kit changed before archiving: $relative"
            }
        }
        $partialArchive = $archivePath + '.staging-' + [guid]::NewGuid().ToString('N')
        if ((Split-Path -Parent $partialArchive) -ne $destinationParent) { throw 'Archive staging must remain beside the new kit.' }
        $archiveFile = [IO.File]::Open($partialArchive, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
        try {
            $writer = [IO.Compression.ZipArchive]::new($archiveFile, [IO.Compression.ZipArchiveMode]::Create, $true)
            try {
                # Explicit names avoid legacy .NET Framework backslash ZIP entries.
                foreach ($relative in ($archiveEntries.Keys | Sort-Object)) {
                    $entry = $writer.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
                    $sourceStream = [IO.File]::OpenRead((Join-Path $destination $relative))
                    $destinationStream = $entry.Open()
                    try { $sourceStream.CopyTo($destinationStream) }
                    finally { $sourceStream.Dispose(); $destinationStream.Dispose() }
                }
            } finally { $writer.Dispose() }
        } finally { $archiveFile.Dispose() }
        $reader = [IO.Compression.ZipFile]::OpenRead($partialArchive)
        try {
            if ($reader.Entries.Count -ne $archiveEntries.Count) { throw 'Source ZIP entry count mismatch.' }
            $seenEntries = @{}
            foreach ($entry in $reader.Entries) {
                if (-not $archiveEntries.ContainsKey($entry.FullName) -or $seenEntries.ContainsKey($entry.FullName)) {
                    throw "Unexpected/duplicate source ZIP entry: $($entry.FullName)"
                }
                $seenEntries[$entry.FullName] = $true
                $hash = [Security.Cryptography.SHA256]::Create()
                $stream = $entry.Open()
                try { $actualHash = [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '') }
                finally { $stream.Dispose(); $hash.Dispose() }
                if ($actualHash -ne $archiveEntries[$entry.FullName]) { throw "Source ZIP hash mismatch: $($entry.FullName)" }
            }
        } finally { $reader.Dispose() }
        # Atomic file rename with no overwrite, even if the final ZIP appeared meanwhile.
        [IO.File]::Move($partialArchive, $archivePath)
        Write-Host "[hpvr.source-kit.archive] status=PASS path=$archivePath files=$($archiveEntries.Count)"
        Write-Host "[hpvr.source-kit.archive] sha256=$((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash)"
    }
} catch {
    Write-Warning "No existing files were removed or overwritten. Inspect any newly created staging, kit or archive at: $destination"
    throw
}
