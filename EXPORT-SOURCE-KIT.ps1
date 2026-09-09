[CmdletBinding()]
param(
    [string]$DestinationPath,
    [switch]$AuditOnly
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

# This exports current files, not HEAD: production changes can be uncommitted
# or untracked. It never traverses the private local/build/retail data trees.
$rootFiles = @(
    '.gitattributes', '.gitignore', 'AGENTS.md', 'CMakeLists.txt', 'README.md',
    'SOURCE-KIT-README.md', 'EXPORT-SOURCE-KIT.ps1', 'BUILD-QUEST-DEBUG.ps1',
    'PACKAGE-QUEST-DEBUG.ps1', 'VERIFY-QUEST-APK.ps1', 'IMPORT-QUEST-DATA.ps1',
    'PREPARE-QUEST-AUDIO.ps1', 'PREPARE-QUEST-FRONTEND.ps1', 'RUN-LATEST-VR.cmd',
    'android/build.gradle', 'android/settings.gradle', 'android/gradle.properties',
    'android/app/build.gradle', 'tools/verify-baseline.ps1', 'tools/test-source-kit-export.ps1',
    'tools/xr-runtime-probe/Cargo.toml', 'tools/xr-runtime-probe/Cargo.lock',
    'tools/xr-runtime-probe/rust-toolchain.toml', 'tools/xr-runtime-probe/build.rs'
)
$optionalRootFiles = @('LICENSE', 'LICENSE.md', 'LICENSE.txt', 'COPYING', 'COPYING.md', 'COPYING.txt', 'NOTICE', 'NOTICE.md', 'NOTICE.txt')
$treeRules = @(
    @{ Path = 'src'; Extensions = @('.cpp', '.c', '.h', '.hpp', '.cmake'); Names = @('CMakeLists.txt') },
    @{ Path = 'android/app/src/main'; Extensions = @('.cpp', '.c', '.h', '.hpp', '.cmake', '.vert', '.frag', '.xml', '.java', '.kt'); Names = @('CMakeLists.txt') },
    @{ Path = 'tools/xr-runtime-probe/src'; Extensions = @('.rs', '.wgsl'); Names = @() },
    @{ Path = 'docs'; Extensions = @('.md'); Names = @() }
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
} catch {
    Write-Warning "Incomplete staging folder preserved for inspection: $staging"
    throw
}
