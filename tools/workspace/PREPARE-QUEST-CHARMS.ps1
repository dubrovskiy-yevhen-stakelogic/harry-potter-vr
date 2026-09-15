#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GamePath,
    [string]$WorkRoot,
    [string]$HostBuildDirectory = 'build',
    [string]$FfmpegPath,
    [switch]$LibraryOnly
)

# Prepare private data without installation, device access or game launch.
$hpvrCharmsArguments = @{
    GamePath=$GamePath; WorkRoot=$WorkRoot; HostBuildDirectory=$HostBuildDirectory;
    FfmpegPath=$FfmpegPath; MapId=3
}
$hpvrCharmsLibraryOnly = $LibraryOnly
. (Join-Path $PSScriptRoot 'PREPARE-QUEST-BROOM.ps1') -LibraryOnly
if (-not $hpvrCharmsLibraryOnly) { Invoke-HpvrBroomPreparation @hpvrCharmsArguments }
