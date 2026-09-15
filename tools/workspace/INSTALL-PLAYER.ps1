#requires -Version 5.1
[CmdletBinding()]
param([string]$ReleaseDirectory, [string]$GamePath)
$ErrorActionPreference='Stop'
Write-Host 'Harry Potter VR - Quest installer'
Write-Host 'This repository contains source code, not a prebuilt APK or game data.'
Write-Host 'Download and extract a player release from the author first.'
Write-Host 'Release page: https://github.com/dubrovskiy-yevhen-stakelogic/harry-potter-vr-source-kit/releases'
if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $ReleaseDirectory=Read-Host 'Extracted player release folder (leave empty to cancel)'
}
if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) { Write-Host 'Cancelled. Nothing was installed.'; return }
$ReleaseDirectory=$ReleaseDirectory.Trim().Trim('"')
if (-not (Test-Path -LiteralPath (Join-Path $ReleaseDirectory 'release-manifest.json') -PathType Leaf)) {
    throw 'Select an extracted player release with release-manifest.json, not the source ZIP or the original PC game folder.'
}
$options=@{ ReleaseRoot=$ReleaseDirectory }
if ([string]::IsNullOrWhiteSpace($GamePath)) { $options.PromptForGamePath=$true }
else { $options.GamePath=$GamePath }
& (Join-Path $PSScriptRoot '../release/INSTALL-HPVR.ps1') @options
