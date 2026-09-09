[CmdletBinding()]
param(
    [string]$InstallRoot = 'C:\Program Files\HP',
    [string]$UserDataRoot = 'C:\Users\user\Documents\Harry Potter',
    [switch]$StrictHashes
)

$ErrorActionPreference = 'Stop'

$expectedHashes = [ordered]@{
    'system\HP.exe'         = '43B2D1471BC36E3290F4474BADF4D0FD84B674E4AA820FE4FCBD75DFBB2903BD'
    'system\Core.dll'       = '60F441EE152E13FA79DE481901645DDC65638B97142E2E3570C1E76E3DE8C788'
    'system\Engine.dll'     = '7756A2A3DF7198D72F4706952196BEE8ADB3B79EDFE7C8B3A5E4D2E3593D8EBC'
    'system\Render.dll'     = '41C0E9939CAC1833978C15BB10A13761B3559AD929F060EC88B6AAE8B96BC55F'
    'system\D3DDrv.dll'     = '7683B11647DAFE3926EFF7D0D055ABBE3D728648A19F5F8A613FD03EFD151599'
    'system\HPBase.u'       = '30B5EF44E9755AA9C020BE9D863E35335C26C2D6988FD0A00A347A98C44E105D'
    'system\HarryPotter.u'  = '5F18066AC7D6A64BA315A19753308613C0819B3944DA551A17BD0F710560CF60'
}

$results = foreach ($relativePath in $expectedHashes.Keys) {
    $absolutePath = Join-Path $InstallRoot $relativePath
    if (-not (Test-Path -LiteralPath $absolutePath -PathType Leaf)) {
        [pscustomobject]@{
            Path = $relativePath
            Status = 'MISSING'
            Hash = $null
        }
        continue
    }

    $actualHash = (Get-FileHash -LiteralPath $absolutePath -Algorithm SHA256).Hash
    [pscustomobject]@{
        Path = $relativePath
        Status = if ($actualHash -eq $expectedHashes[$relativePath]) { 'MATCH' } else { 'DIFFERENT' }
        Hash = $actualHash
    }
}

$results | Format-Table -AutoSize

$runtimeLog = Join-Path $UserDataRoot 'HP.log'
$runtimeEvidence = [pscustomobject]@{
    LogPath = $runtimeLog
    Exists = Test-Path -LiteralPath $runtimeLog -PathType Leaf
    Version433 = $false
    LoadedStartup = $false
    SpawnedWand = $false
    CleanExit = $false
}

if ($runtimeEvidence.Exists) {
    $logText = Get-Content -LiteralPath $runtimeLog -Raw
    $runtimeEvidence.Version433 = $logText -match '(?m)^Init: Version: 433\s*$'
    $runtimeEvidence.LoadedStartup = $logText -match 'LoadMap: Startup\.unr'
    $runtimeEvidence.SpawnedWand = $logText -match 'spawning weap Startup\.baseWand0'
    $runtimeEvidence.CleanExit = $logText -match 'Exit: Object subsystem successfully closed\.'
}

$runtimeEvidence | Format-List

$missingOrDifferent = @($results | Where-Object Status -ne 'MATCH')
$runtimeFailed = -not (
    $runtimeEvidence.Exists -and
    $runtimeEvidence.Version433 -and
    $runtimeEvidence.LoadedStartup -and
    $runtimeEvidence.SpawnedWand -and
    $runtimeEvidence.CleanExit
)

if ($StrictHashes -and $missingOrDifferent.Count -gt 0) {
    throw "Baseline hash verification failed for $($missingOrDifferent.Count) file(s)."
}

if ($runtimeFailed) {
    Write-Warning 'Current runtime log does not contain every recorded baseline marker.'
}
