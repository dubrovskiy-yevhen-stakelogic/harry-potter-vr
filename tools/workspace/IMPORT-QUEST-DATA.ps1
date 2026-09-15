[CmdletBinding()]
param(
    [string]$SourceRoot = 'C:\Program Files\HP',
    [string]$DeviceSerial,
    [switch]$Copy
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../release/INSTALL-HPVR.ps1') -LibraryOnly
$SourceRoot = [System.IO.Path]::GetFullPath($SourceRoot)
$adb = 'C:\Dev\android-toolchain\sdk\platform-tools\adb.exe'
$packageName = 'io.github.hpvr.quest'
$targetRoot = "/sdcard/Android/data/$packageName/files/HP"
$contentDirectories = @('Maps', 'Music', 'Sounds', 'system', 'Textures')
$excludedExtensions = @('.dll', '.exe', '.log', '.sys', '.dat', '.lnk', '.hlp', '.ico')
$requiredRelativePaths = @(
    'system/HPBase.u',
    'system/HarryPotter.u',
    'Maps/Lev_Tut1.unr'
)

if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
    throw "HP source root does not exist: $SourceRoot"
}
foreach ($relativePath in $requiredRelativePaths) {
    $localPath = Join-Path $SourceRoot ($relativePath -replace '/', '\')
    if (-not (Test-Path -LiteralPath $localPath -PathType Leaf)) {
        throw "Required HP data file is missing: $localPath"
    }
}

$files = @(
    foreach ($directoryName in $contentDirectories) {
        $directoryPath = Join-Path $SourceRoot $directoryName
        if (-not (Test-Path -LiteralPath $directoryPath -PathType Container)) {
            throw "Required HP content directory is missing: $directoryPath"
        }
        Get-ChildItem -LiteralPath $directoryPath -Recurse -File |
            Where-Object { $excludedExtensions -notcontains $_.Extension.ToLowerInvariant() }
    }
)
if ($files.Count -eq 0) {
    throw 'No transferable HP data files were found.'
}

$totalBytes = ($files | Measure-Object -Property Length -Sum).Sum
$totalMiB = [Math]::Round($totalBytes / 1MB, 2)
Write-Host '[hpvr.quest.data.plan] status=READY'
Write-Host "[hpvr.quest.data.plan] source=$SourceRoot"
Write-Host "[hpvr.quest.data.plan] target=$targetRoot"
Write-Host "[hpvr.quest.data.plan] files=$($files.Count) bytes=$totalBytes mib=$totalMiB"
Write-Host '[hpvr.quest.data.plan] excludes=Windows executables/DLLs, logs, drivers, uninstall data, Help, Support, saves'

if (-not $Copy) {
    Write-Host '[hpvr.quest.data.plan] action=NONE rerun_with=-Copy'
    return
}
if (-not (Test-Path -LiteralPath $adb -PathType Leaf)) {
    throw "adb is missing: $adb"
}

$deviceRows = @(
    & $adb devices |
        Select-Object -Skip 1 |
        Where-Object { $_ -match '^([^\s]+)\s+device\s*$' } |
        ForEach-Object {
            [regex]::Match($_, '^([^\s]+)\s+device\s*$').Groups[1].Value
        }
)
if ([string]::IsNullOrWhiteSpace($DeviceSerial)) {
    if ($deviceRows.Count -ne 1) {
        throw "Expected exactly one authorized device, found $($deviceRows.Count). Pass -DeviceSerial when needed."
    }
    $DeviceSerial = $deviceRows[0]
} elseif ($deviceRows -notcontains $DeviceSerial) {
    throw "Requested device is not connected and authorized: $DeviceSerial"
}
$deviceArgs = @('-s', $DeviceSerial)

function Invoke-AdbChecked {
    param([Parameter(Mandatory)][string[]]$Arguments)

    & $adb @deviceArgs @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
    }
}

Invoke-AdbChecked -Arguments @('shell', "mkdir -p '$targetRoot'")
$relativeDirectories = @(
    $files |
        ForEach-Object {
            $relative = [System.IO.Path]::GetRelativePath($SourceRoot, $_.DirectoryName)
            $relative -replace '\\', '/'
        } |
        Sort-Object -Unique
)
foreach ($relativeDirectory in $relativeDirectories) {
    if ($relativeDirectory.Contains("'")) {
        throw "Unsupported apostrophe in remote directory name: $relativeDirectory"
    }
    Invoke-AdbChecked -Arguments @(
        'shell',
        "mkdir -p '$targetRoot/$relativeDirectory'"
    )
}

$index = 0
foreach ($file in $files) {
    ++$index
    $relativePath = [System.IO.Path]::GetRelativePath($SourceRoot, $file.FullName) -replace '\\', '/'
    $remotePath = "$targetRoot/$relativePath"
    Write-Progress -Activity 'Copying owned HP data to Quest' `
        -Status "$index / $($files.Count): $relativePath" `
        -PercentComplete (($index * 100.0) / $files.Count)
    Invoke-AdbChecked -Arguments @('push', '--sync', $file.FullName, $remotePath)
}
Write-Progress -Activity 'Copying owned HP data to Quest' -Completed

foreach ($relativePath in $requiredRelativePaths) {
    $localPath = Join-Path $SourceRoot ($relativePath -replace '/', '\')
    $localHash = (Get-FileHash -LiteralPath $localPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $remotePath = "$targetRoot/$relativePath"
    $remoteResult = (& $adb @deviceArgs shell "sha256sum '$remotePath'" 2>&1) -join "`n"
    $remoteHashMatch = [regex]::Match($remoteResult, '^([0-9a-fA-F]{64})\s')
    if ($LASTEXITCODE -ne 0 -or -not $remoteHashMatch.Success) {
        throw "Could not hash imported sentinel: $remotePath"
    }
    if ($remoteHashMatch.Groups[1].Value.ToLowerInvariant() -ne $localHash) {
        throw "Imported sentinel hash mismatch: $relativePath"
    }
    Write-Host "[hpvr.quest.data.verify] path=$relativePath sha256=$localHash status=MATCH"
}

Set-HpvrDataPermissions $adb $deviceArgs @($files | ForEach-Object {
    [System.IO.Path]::GetRelativePath($SourceRoot, $_.FullName) -replace '\\', '/'
})
Write-Host '[hpvr.quest.data.import] status=PASS'
Write-Host "[hpvr.quest.data.import] device=$DeviceSerial files=$($files.Count) bytes=$totalBytes"
Write-Host '[hpvr.quest.data.import] apk_install=NOT_PERFORMED app_launch=NOT_PERFORMED'
