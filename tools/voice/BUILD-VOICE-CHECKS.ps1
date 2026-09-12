[CmdletBinding()]
param([string]$CMakePath = 'cmake.exe', [switch]$Offline)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
& (Join-Path $PSScriptRoot 'FETCH-VOICE-DEPENDENCIES.ps1') -Offline:$Offline
$build = Join-Path $repo 'local/neural-voice-host-checks'
& $CMakePath -S $PSScriptRoot -B $build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Voice check CMake configure failed.' }
& $CMakePath --build $build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Voice check build failed.' }
& (Join-Path ([IO.Path]::GetDirectoryName((Get-Command $CMakePath).Source)) 'ctest.exe') --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Voice checks failed.' }
& (Join-Path $PSScriptRoot 'TEST-VOICE-APK-PAYLOAD.ps1')
Write-Host 'Offline policy/acoustic-negative checks passed. This does not establish headset recognition accuracy or performance.'
