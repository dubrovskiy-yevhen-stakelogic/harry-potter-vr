#requires -Version 5.1
[CmdletBinding()]
param([string]$FixtureRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'INSTALL-HPVR.ps1') -LibraryOnly
$script:checks = 0
function Assert-HpvrTest([bool]$Pass, [string]$Name) {
    if (-not $Pass) { throw "Failed installer test: $Name" }
    ++$script:checks
}
function Assert-HpvrReject([scriptblock]$Action, [string]$Name) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert-HpvrTest $rejected $Name
}
if ([string]::IsNullOrWhiteSpace($FixtureRoot)) {
    $FixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('hpvr-player-tests-' + [Guid]::NewGuid().ToString('N'))
}
$fixture = Get-HpvrFullPath $FixtureRoot
if (Test-Path -LiteralPath $fixture) { throw 'Synthetic test fixture directory must not already exist.' }
Assert-HpvrNoLinks $fixture
New-Item -ItemType Directory -Path $fixture | Out-Null
$owned = Join-Path $fixture 'Owned'
$kit = Join-Path $fixture 'Kit'
New-Item -ItemType Directory -Path (Join-Path $owned 'system'), (Join-Path $owned 'Maps'), (Join-Path $kit 'tools') | Out-Null

Assert-HpvrTest (Test-HpvrWithin (Join-Path $owned 'Maps\test.unr') $owned) 'Child contained'
Assert-HpvrTest (-not (Test-HpvrWithin ($owned + '-other\Maps\test.unr') $owned)) 'Sibling-prefix rejected'
Assert-HpvrTest (-not (Test-HpvrWithin (Join-Path $owned '..\outside.bin') $owned)) 'Traversal containment rejected'
Assert-HpvrTest ((Get-HpvrFullPath 'C:\') -eq 'C:\') 'Drive root stays absolute'
Assert-HpvrTest (Test-HpvrWithin 'C:\one\two' 'C:\') 'Root containment boundary'
Assert-HpvrTest ((Get-HpvrRelative (Join-Path $owned 'Maps\test.unr') $owned) -eq 'Maps/test.unr') 'Relative path normalized'
Assert-HpvrReject { Get-HpvrRelative $fixture $owned } 'Relative escape rejected'
Assert-HpvrReject { Get-HpvrRelative $owned $owned } 'Root not a file'
foreach ($path in @('../evil', 'one/../evil', '/absolute', 'C:/absolute', 'one//two', 'one/./two', 'one\two', "one/evil'file", 'one/file ', 'one/file.', 'one/evil:stream', 'one/$x')) {
    Assert-HpvrReject { Assert-HpvrSafeRelative $path } ('Invalid name ' + $path)
}
foreach ($path in @('system/HarryPotter.u', 'Cache/Audio/voice.0123abcd.s16', 'tools/hpvr_quest_frontend_probe.exe', 'Read me.md')) {
    Assert-HpvrSafeRelative $path
    Assert-HpvrTest $true ('Accepted name ' + $path)
}
[IO.File]::WriteAllBytes((Join-Path $owned 'system\test.u'), [byte[]](1,2,3))
[IO.File]::WriteAllBytes((Join-Path $owned 'system\HP.exe'), [byte[]](77,90))
$inputData = Get-HpvrOwnedInput $owned (Join-Path $owned 'system\test.u')
Assert-HpvrTest ($inputData.Relative -eq 'system/test.u') 'Owned package accepted'
Assert-HpvrReject { Get-HpvrOwnedInput $owned (Join-Path $owned 'system\HP.exe') } 'Executable cannot be imported'
Assert-HpvrReject { Get-HpvrOwnedInput $owned (Join-Path $owned 'system\missing.u') } 'Missing dependency rejected'
[IO.File]::WriteAllBytes((Join-Path $fixture 'outside.u'), [byte[]](1))
Assert-HpvrReject { Get-HpvrOwnedInput $owned (Join-Path $fixture 'outside.u') } 'External source rejected'

$mono = Join-Path $fixture 'mono.s16'
$samples = New-Object byte[] 960
$samples[500] = 1
[IO.File]::WriteAllBytes($mono, $samples)
Assert-HpvrPcm $mono 1
Assert-HpvrTest $true 'Valid PCM accepted'
Assert-HpvrReject { Assert-HpvrPcm $mono 3 } 'Invalid channels rejected'
$silent = Join-Path $fixture 'silent.s16'
[IO.File]::WriteAllBytes($silent, (New-Object byte[] 960))
Assert-HpvrReject { Assert-HpvrPcm $silent 1 } 'Silent PCM rejected'
$odd = Join-Path $fixture 'odd.s16'
[IO.File]::WriteAllBytes($odd, (New-Object byte[] 961))
Assert-HpvrReject { Assert-HpvrPcm $odd 1 } 'Misaligned PCM rejected'
$short = Join-Path $fixture 'short.s16'
[IO.File]::WriteAllBytes($short, [byte[]](1,2))
Assert-HpvrReject { Assert-HpvrPcm $short 1 } 'Truncated PCM rejected'

$entries = @()
foreach ($name in @('HPVR-Quest-Demo.apk', 'tools/hpvr_hp1_package_graph.exe', 'tools/hpvr_hp1_sound_probe.exe',
        'tools/hpvr_quest_frontend_probe.exe', 'tools/hpvr_quest_intro_probe.exe')) {
    $file = Join-Path $kit $name
    [IO.File]::WriteAllBytes($file, [byte[]](10,20,30,40))
    $entries += [pscustomobject]@{ path = $name; sha256 = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash }
}
$manifestPath = Join-Path $kit 'release-manifest.json'
$manifest = [pscustomobject]@{ schema = 1; packageName = 'io.github.hpvr.quest'; apk = 'HPVR-Quest-Demo.apk'; files = $entries }
function Write-FixtureManifest($Object) { $Object | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8 }
Write-FixtureManifest $manifest
$verified = Read-HpvrReleaseManifest $kit
Assert-HpvrTest ($verified.packageName -eq 'io.github.hpvr.quest') 'Complete hashed release accepted'
$manifest.schema = 2
Write-FixtureManifest $manifest
Assert-HpvrReject { Read-HpvrReleaseManifest $kit } 'Unknown manifest schema rejected'
$manifest.schema = 1
$manifest.packageName = 'other.game'
Write-FixtureManifest $manifest
Assert-HpvrReject { Read-HpvrReleaseManifest $kit } 'Wrong package rejected'
$manifest.packageName = 'io.github.hpvr.quest'
$manifest.apk = '../evil.apk'
Write-FixtureManifest $manifest
Assert-HpvrReject { Read-HpvrReleaseManifest $kit } 'Escaping APK name rejected'
$manifest.apk = 'HPVR-Quest-Demo.apk'
$manifest.files = @($entries[0..3])
Write-FixtureManifest $manifest
Assert-HpvrReject { Read-HpvrReleaseManifest $kit } 'Unverified helper rejected'
$manifest.files = @($entries) + @($entries[0])
Write-FixtureManifest $manifest
Assert-HpvrReject { Read-HpvrReleaseManifest $kit } 'Duplicate manifest entry rejected'
$manifest.files = $entries
Write-FixtureManifest $manifest
[IO.File]::WriteAllBytes((Join-Path $kit 'HPVR-Quest-Demo.apk'), [byte[]](99))
Assert-HpvrReject { Read-HpvrReleaseManifest $kit } 'Modified APK rejected'
Assert-HpvrReject { Get-HpvrTool (Join-Path $fixture 'no-adb.exe') 'adb.exe' @() } 'Missing explicit tool rejected'
Write-Output "PLAYER_INSTALL_TESTS=PASS checks=$script:checks device_actions=0 native_tools_executed=0 fixtures=$fixture"
