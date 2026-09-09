#requires -Version 5.1
[CmdletBinding()]
param([string]$FixtureRoot, [switch]$Online)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'INSTALL-HPVR.ps1') -LibraryOnly
if (-not $FixtureRoot) { $FixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('hpvr-adb-tests-' + [Guid]::NewGuid().ToString('N')) }
$fixture = Get-HpvrFullPath $FixtureRoot
if (Test-Path -LiteralPath $fixture) { throw 'Use a fresh fixture directory.' }
Assert-HpvrNoLinks $fixture
New-Item -ItemType Directory -Path $fixture | Out-Null
$script:checks = 0
function Check([bool]$ok, [string]$name) { if (-not $ok) { throw "Failed: $name" }; ++$script:checks }
function Reject([scriptblock]$action, [string]$name) { $failed = $false; try { & $action | Out-Null } catch { $failed = $true }; Check $failed $name }
$script:consent = 'YES'
$script:prompts = 0
function Read-Host([string]$Prompt) { ++$script:prompts; return $script:consent }
$oldPath = $env:PATH
try {
    $env:PATH = ''
    if ($Online) {
        $spec = Get-HpvrAdbSpec
        $exe = Get-HpvrAdb '' @() (Join-Path $fixture 'online cache')
        $version = @(& $exe version)
        Check ($LASTEXITCODE -eq 0 -and ($version -join ' ') -match '36\.0\.2') 'Downloaded ADB loads its DLLs and reports pinned version'
        Check ((Get-HpvrAdb '' @() (Split-Path $exe) -NoDownload) -eq $exe) 'Real cache works offline'
        Check ($script:prompts -eq 1) 'Verified cache does not prompt again'
    } else {
        Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
        $zip = Join-Path $fixture 'fixture.zip'
        $fake = [byte[]](77,90,1,2,3,4,5)
        $localExe = Join-Path $fixture 'local.exe'
        [IO.File]::WriteAllBytes($localExe, $fake)
        $hash = (Get-FileHash $localExe -Algorithm SHA256).Hash
        $script:spec = [pscustomobject]@{ Version = 'test'; Url = 'https://example.invalid/test.zip'; ArchiveSha256 = ''; Files = [ordered]@{
            'adb.exe' = $hash; 'AdbWinApi.dll' = $hash; 'AdbWinUsbApi.dll' = $hash; 'NOTICE.txt' = $hash; 'source.properties' = $hash
        } }
        $archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($name in @($script:spec.Files.Keys | ForEach-Object { 'platform-tools/' + $_ }) + @('../outside.exe')) {
                $entry = $archive.CreateEntry($name); $s = $entry.Open()
                try { $s.Write($fake, 0, $fake.Length) } finally { $s.Dispose() }
            }
        } finally { $archive.Dispose() }
        $script:spec.ArchiveSha256 = (Get-FileHash $zip -Algorithm SHA256).Hash
        function Get-HpvrAdbSpec { return $script:spec }
        $script:downloads = 0; $script:failNetwork = $false
        function Save-HpvrAdbDownload([string]$Url, [string]$Destination) {
            ++$script:downloads
            if ($script:failNetwork) { throw 'Synthetic network failure' }
            Copy-Item -LiteralPath $zip -Destination $Destination
        }
        $cache = Join-Path $fixture 'cache with spaces'
        Check ((Get-HpvrAdb $localExe @() $cache) -eq $localExe) 'Explicit tool wins'
        Check ((Get-HpvrAdb '' @($localExe) $cache) -eq $localExe) 'Installed candidate wins'
        Reject { Get-HpvrAdb (Join-Path $fixture 'missing.exe') @() $cache } 'Invalid explicit tool does not download'
        Reject { Get-HpvrAdb '' @() $cache -NoDownload } 'Missing offline tool fails'
        Check ($script:downloads -eq 0 -and $script:prompts -eq 0) 'Existing or offline tools require no network or consent'
        $script:consent = 'NO'
        Reject { Get-HpvrAdb '' @() $cache } 'Declined license cancels'
        Check ($script:downloads -eq 0) 'No download before consent'
        $script:consent = 'YES'
        $exe = Get-HpvrAdb '' @() $cache
        foreach ($name in $script:spec.Files.Keys) { Check ((Get-FileHash (Join-Path $cache $name) -Algorithm SHA256).Hash -eq $hash) "Verified $name" }
        Check (-not (Test-Path (Join-Path $fixture 'outside.exe'))) 'ZIP traversal not extracted'
        Check (@(Get-ChildItem $cache -File | Where-Object { $_.Name -match '\.zip$|\.pending$' }).Count -eq 0) 'Temporary files cleaned'
        $prompts = $script:prompts
        Check ((Get-HpvrAdb '' @() $cache -NoDownload) -eq $exe) 'Verified cache works offline'
        Check ($script:downloads -eq 1 -and $script:prompts -eq $prompts) 'Cache bypasses network and consent'
        [IO.File]::WriteAllBytes((Join-Path $cache 'AdbWinApi.dll'), [byte[]](9))
        Reject { Get-HpvrAdb '' @() $cache -NoDownload } 'Tampered DLL rejected even with valid EXE'
        $null = Get-HpvrAdb '' @() $cache
        Check ($script:downloads -eq 2 -and (Get-FileHash (Join-Path $cache 'AdbWinApi.dll')).Hash -eq $hash) 'Damaged cache repaired'
        $good = $script:spec.ArchiveSha256; $script:spec.ArchiveSha256 = '0' * 64
        Reject { Get-HpvrAdb '' @() (Join-Path $fixture 'bad zip') } 'Archive pin enforced'
        $script:spec.ArchiveSha256 = $good
        $script:spec.Files['AdbWinUsbApi.dll'] = '0' * 64
        $bad = Join-Path $fixture 'bad DLL'
        Reject { Get-HpvrAdb '' @() $bad } 'Extracted DLL pin enforced'
        Check (-not (Test-Path (Join-Path $bad 'adb.exe'))) 'No partial installation after verification failure'
        $script:spec.Files['AdbWinUsbApi.dll'] = $hash
        $script:spec.Files['missing.dll'] = $hash
        Reject { Get-HpvrAdb '' @() (Join-Path $fixture 'missing entry') } 'Missing dependency rejected'
        $script:spec.Files.Remove('missing.dll')
        $script:failNetwork = $true
        Reject { Get-HpvrAdb '' @() (Join-Path $fixture 'offline') } 'Network failure returns manual fallback'
    }
    Check ([string]::IsNullOrEmpty($env:PATH)) 'PATH unchanged'
} finally { $env:PATH = $oldPath }
Write-Output "ADB_BOOTSTRAP_TESTS=PASS checks=$script:checks online=$Online device_actions=0 fixture=$fixture"

