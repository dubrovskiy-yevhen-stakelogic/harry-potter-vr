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
[IO.File]::WriteAllBytes((Join-Path $owned 'Maps\Lev_Tut1.unr'), [byte[]](1,2,3))
$demoMaps=@(Get-HpvrMapInputs $owned)
Assert-HpvrTest ($demoMaps.Count -eq 1 -and $demoMaps[0].Relative -eq 'Maps/Lev_Tut1.unr') 'Released default imports only map zero'
Assert-HpvrReject { Get-HpvrMapInputs $owned $true } 'Requested missing challenge map rejected'
[IO.File]::WriteAllBytes((Join-Path $owned 'Maps\Lev_Tut1b.unr'), [byte[]](4,5,6))
$challengeMaps=@(Get-HpvrMapInputs $owned $true)
Assert-HpvrTest ($challengeMaps.Count -eq 2 -and $challengeMaps[1].Relative -eq 'Maps/Lev_Tut1b.unr') 'Opt-in includes exact challenge map'
Assert-HpvrTest (@(Get-HpvrMapInputs $owned).Count -eq 1) 'Challenge presence does not change released default'
Assert-HpvrReject { Get-HpvrMapInputs $owned $true $true } 'Requested missing broom map rejected'
[IO.File]::WriteAllBytes((Join-Path $owned 'Maps\Lev_Tut2.unr'), [byte[]](7,8,9))
$broomMaps = @(Get-HpvrMapInputs $owned $true $true)
Assert-HpvrTest ($broomMaps.Count -eq 3 -and $broomMaps[2].Relative -eq 'Maps/Lev_Tut2.unr') 'Three-map selection includes exact broom map'
Assert-HpvrReject { Get-HpvrMapInputs $owned $false $true } 'Broom map requires preceding challenge selection'
Assert-HpvrTest (@(Get-HpvrMapInputs $owned $true).Count -eq 2) 'Broom file presence does not expand older release selection'
Assert-HpvrReject { Get-HpvrMapInputs $owned $true $true $true } 'Missing Charms map rejected'
[IO.File]::WriteAllBytes((Join-Path $owned 'Maps/Lev_Tut3.unr'), [byte[]](10,11,12))
Assert-HpvrTest (@(Get-HpvrMapInputs $owned $true $true $true).Count -eq 4) 'Four-map selection includes Charms'
Assert-HpvrReject { Get-HpvrMapInputs $owned $true $false $true } 'Charms cannot bypass broom map'
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
$legacyMaps = @(Get-HpvrReleaseMapIds $verified)
Assert-HpvrTest ($legacyMaps.Count -eq 1 -and $legacyMaps[0] -eq 0) 'Missing map metadata preserves the one-map release contract'
$explicitMaps = @(Get-HpvrReleaseMapIds $verified -IncludeChallenge)
Assert-HpvrTest ($explicitMaps.Count -eq 2 -and $explicitMaps[1] -eq 1) 'Explicit challenge option remains compatible with old manifests'
$alpha = [pscustomobject]@{ schema=1; packageName='io.github.hpvr.quest'; apk='HPVR-Quest-Demo.apk'; versionName='0.1.1-alpha'; versionCode=54; mapIds=@(0,1); files=$entries }
Write-FixtureManifest $alpha
$verifiedAlpha = Read-HpvrReleaseManifest $kit
$alphaMaps = @(Get-HpvrReleaseMapIds $verifiedAlpha)
Assert-HpvrTest ($alphaMaps.Count -eq 2 -and $alphaMaps[0] -eq 0 -and $alphaMaps[1] -eq 1) 'Two-map alpha selects both maps without a CLI switch'
Assert-HpvrTest (@(Get-HpvrReleaseMapIds $verifiedAlpha -IncludeChallenge).Count -eq 2) 'Explicit challenge option does not duplicate declared maps'
$broomRelease = [pscustomobject]@{ schema=1; packageName='io.github.hpvr.quest'; apk='HPVR-Quest-Demo.apk'; versionName='0.1.2-alpha'; versionCode=57; mapIds=@(0,1,2); files=$entries }
Write-FixtureManifest $broomRelease
$verifiedBroom = Read-HpvrReleaseManifest $kit
$broomMapIds = @(Get-HpvrReleaseMapIds $verifiedBroom)
Assert-HpvrTest (($broomMapIds -join ',') -eq '0,1,2') 'Three-map release selects every map automatically'
Assert-HpvrTest ((@(Get-HpvrReleaseMapIds $verifiedBroom -IncludeChallenge) -join ',') -eq '0,1,2') 'Legacy challenge flag cannot remove the declared broom map'
Assert-HpvrReject { Get-HpvrReleaseMapIds ([pscustomobject]@{versionCode=56;mapIds=@(0,1,2)}) } 'Development APK cannot claim the new public three-map contract'
Assert-HpvrTest (@(Get-HpvrReleaseMapIds ([pscustomobject]@{versionCode=57})).Count -eq 1) 'New version alone cannot silently expand a legacy manifest'
Assert-HpvrTest ((@(Get-HpvrReleaseMapIds ([pscustomobject]@{versionCode=71;mapIds=@(0,1,2,3)})) -join ',') -eq '0,1,2,3') 'Current release supports four maps'
Assert-HpvrReject { Get-HpvrReleaseMapIds ([pscustomobject]@{versionCode=70;mapIds=@(0,1,2,3)}) } 'Old APK cannot claim four-map installer contract'
foreach ($badMaps in @(@(0,1,1), @(0,2,1), @(0,1,3), @(0,1,2,3))) {
    Assert-HpvrReject { Get-HpvrReleaseMapIds ([pscustomobject]@{versionCode=57;mapIds=$badMaps}) } 'Three-map release rejects duplicate, reordered, or unsupported maps'
}
$alpha.mapIds = @(0)
$alpha.versionCode = 37
Write-FixtureManifest $alpha
$verifiedSingle = Read-HpvrReleaseManifest $kit
Assert-HpvrTest ($verifiedSingle.mapIds -is [Array] -and @(Get-HpvrReleaseMapIds $verifiedSingle).Count -eq 1) 'Explicit single-map JSON array supports the original APK'
$alpha.mapIds = @(0,1)
Write-FixtureManifest $alpha
Assert-HpvrReject {Read-HpvrReleaseManifest $kit} 'Original APK cannot declare challenge support'
$alpha.versionCode = 54
foreach ($invalid in @(
    [pscustomobject]@{Name='null';Value=$null},
    [pscustomobject]@{Name='scalar';Value=0},
    [pscustomobject]@{Name='string';Value='0,1'},
    [pscustomobject]@{Name='empty';Value=@()},
    [pscustomobject]@{Name='strings';Value=@('0','1')},
    [pscustomobject]@{Name='boolean';Value=@($false)},
    [pscustomobject]@{Name='fractional';Value=@(0,0.5)},
    [pscustomobject]@{Name='duplicate';Value=@(0,0)},
    [pscustomobject]@{Name='reversed';Value=@(1,0)},
    [pscustomobject]@{Name='challenge-only';Value=@(1)},
    [pscustomobject]@{Name='unknown';Value=@(0,2)},
    [pscustomobject]@{Name='extra';Value=@(0,1,2)}
)) {
    $alpha.mapIds = $invalid.Value
    Write-FixtureManifest $alpha
    Assert-HpvrReject {Read-HpvrReleaseManifest $kit} ('Malformed release map metadata rejected: ' + $invalid.Name)
    Assert-HpvrReject {Get-HpvrReleaseMapIds $alpha -IncludeChallenge} ('CLI switch cannot bypass malformed metadata: ' + $invalid.Name)
}
Assert-HpvrReject {Get-HpvrReleaseMapIds ([pscustomobject]@{mapIds=@(0,1)})} 'Declared challenge requires APK version metadata'
Assert-HpvrReject {Get-HpvrReleaseMapIds ([pscustomobject]@{versionCode=54;mapIds=@([double]0)})} 'In-memory floating-point map IDs are rejected'
$alpha.mapIds = @(0,1)
Write-FixtureManifest $manifest
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

# Exercise the map-selection/closure plumbing without executing any native tool.
foreach($relative in @(
    'system/HPBase.u','system/HarryPotter.u','system/HPMenu.u','system/HPParticle.u','system/HProps.u','system/HPSounds.u',
    'system/hpmenu.int','system/hpdialog.int','Textures/MenuArt.utx','Textures/StoryBookTest.utx',
    'Sounds/AllDialog.uax','Sounds/Magic_sfx.uax','Sounds/Ambient.uax','Sounds/Menu_sfx.uax','Sounds/Hub1_sfx.uax',
    'Music/JS_HP_Title_Screen_v2.umx',
    'Music/JS_StoryBook_v2_mx.umx','Music/JS_Opening_Castle_Fly_Through_mx.umx','Music/happy_hogwarts_mxlp1.umx')){
    $file=Join-Path $owned $relative
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($file)) -Force | Out-Null
    [IO.File]::WriteAllBytes($file,[byte[]](1,2,3))
}
$script:graphCalls=[Collections.Generic.List[string]]::new()
$script:graphMode='valid'
function Invoke-HpvrChecked([string]$Executable,[string[]]$Arguments,[string]$Label){
    $script:graphCalls.Add($Arguments[1])
    $mapPath=if($script:graphMode -eq 'escape'){Join-Path $fixture 'outside.u'}else{$Arguments[1]}
    "package=Map kind=data path=$mapPath version=69 direct_dependencies=2 native_companion=0"
    if($script:graphMode -eq 'truncated'){return}
    "package=HPBase kind=data path=$(Join-Path $owned 'system/HPBase.u') version=69 direct_dependencies=0 native_companion=0"
    "package=HarryPotter kind=data path=$(Join-Path $owned 'system/HarryPotter.u') version=69 direct_dependencies=1 native_companion=0"
}
$closure=Get-HpvrDependencySet $owned 'synthetic-graph.exe'
Assert-HpvrTest ($script:graphCalls.Count -eq 1 -and $closure.Inputs.Relative -notcontains 'Maps/Lev_Tut1b.unr') 'Default dependency scan excludes challenge'
foreach($relative in @('Sounds/Menu_sfx.uax','Sounds/Hub1_sfx.uax')) {
    Assert-HpvrTest (@($closure.Inputs | Where-Object {$_.Relative -ceq $relative -and $_.Source -eq (Join-Path $owned $relative)}).Count -eq 1) ("Single-map closure includes exactly one owned $relative even when absent from the map graph")
}
$script:graphCalls.Clear()
$closure=Get-HpvrDependencySet $owned 'synthetic-graph.exe' $true
Assert-HpvrTest ($script:graphCalls.Count -eq 2 -and $closure.Inputs.Relative -contains 'Maps/Lev_Tut1b.unr') 'Opt-in scans both dependency closures'
Assert-HpvrTest (@($closure.Inputs | Where-Object {$_.Relative -eq 'system/HPBase.u'}).Count -eq 1) 'Shared packages deduplicated'
foreach($relative in @('Sounds/Menu_sfx.uax','Sounds/Hub1_sfx.uax')) {
    Assert-HpvrTest (@($closure.Inputs | Where-Object {$_.Relative -ceq $relative -and $_.Source -eq (Join-Path $owned $relative)}).Count -eq 1) ("Two-map closure includes exactly one owned $relative even when absent from the map graph")
}
$script:graphCalls.Clear()
$closure=Get-HpvrDependencySet $owned 'synthetic-graph.exe' ($alphaMaps -contains 1)
Assert-HpvrTest ($script:graphCalls.Count -eq 2 -and $closure.Inputs.Relative -contains 'Maps/Lev_Tut1.unr' -and $closure.Inputs.Relative -contains 'Maps/Lev_Tut1b.unr') 'Release-selected maps drive both dependency closures without CLI opt-in'
foreach($relative in @('Sounds/Menu_sfx.uax','Sounds/Hub1_sfx.uax')) {
    Assert-HpvrTest (@($closure.Inputs | Where-Object {$_.Relative -ceq $relative -and $_.Source -eq (Join-Path $owned $relative)}).Count -eq 1) ("Alpha manifest closure includes exactly one owned $relative")
}
$script:graphMode='escape'
Assert-HpvrReject {Get-HpvrDependencySet $owned 'synthetic-graph.exe' $true} 'Graph cannot smuggle an external package'
$script:graphMode='truncated'
Assert-HpvrReject {Get-HpvrDependencySet $owned 'synthetic-graph.exe' $true} 'Incomplete graph fails closed'
$script:graphMode='valid'
$script:graphCalls.Clear()
$closure=Get-HpvrDependencySet $owned 'synthetic-graph.exe' ($broomMapIds -contains 1) ($broomMapIds -contains 2)
Assert-HpvrTest ($script:graphCalls.Count -eq 3 -and $closure.Inputs.Relative -contains 'Maps/Lev_Tut2.unr') 'Three-map release scans broom dependency closure'
Assert-HpvrTest (@($closure.Inputs | Where-Object {$_.Relative -eq 'system/HPBase.u'}).Count -eq 1) 'Three-map closure deduplicates shared packages'

# Scene preparation stays opt-in by release capability, never by the presence
# of an arbitrary EXE beside an older installer. All native work below is mocked.
foreach ($entry in $entries) { [IO.File]::WriteAllBytes((Join-Path $kit $entry.path), [byte[]](10,20,30,40)) }
$legacy = [pscustomobject]@{ schema=1; packageName='io.github.hpvr.quest'; apk='HPVR-Quest-Demo.apk'; versionCode=37; files=$entries }
Write-FixtureManifest $legacy
$verified = Read-HpvrReleaseManifest $kit
Assert-HpvrTest ($null -eq (Get-HpvrScenePreparationTool $kit $verified)) 'Legacy release explicitly uses runtime preparation fallback'
$sceneToolPath = Join-Path $kit 'tools/hpvr_quest_prepare_assets.exe'
[IO.File]::WriteAllBytes($sceneToolPath, [byte[]](77,90,10,20))
Assert-HpvrTest ($null -eq (Get-HpvrScenePreparationTool $kit $verified)) 'Unlisted preparation EXE cannot enable old release capability'
$sceneToolEntry = [pscustomobject]@{ path='tools/hpvr_quest_prepare_assets.exe'; sha256=(Get-FileHash -LiteralPath $sceneToolPath -Algorithm SHA256).Hash }
$modern = [pscustomobject]@{ schema=1; packageName='io.github.hpvr.quest'; apk='HPVR-Quest-Demo.apk'; versionCode=43; preparedSceneVersion=1; files=@($entries)+@($sceneToolEntry) }
Write-FixtureManifest $modern
$verified = Read-HpvrReleaseManifest $kit
Assert-HpvrTest ((Get-HpvrScenePreparationTool $kit $verified) -eq $sceneToolPath) 'Matching hashed scene preparation tool selected'
Assert-HpvrTest (@(Get-HpvrReleaseMapIds $verified).Count -eq 1) 'An older prepared-scene manifest without map metadata stays one-map'
Assert-HpvrTest (@(Get-HpvrReleaseMapIds $verified -IncludeChallenge).Count -eq 2) 'Legacy prepared-scene manifest still permits explicit challenge opt-in'
Assert-HpvrTest ($null -eq (Get-HpvrScenePreparationTool $kit $verified -Skip)) 'Explicit diagnostic opt-out avoids scene preparation'
$modern.versionCode=42
Write-FixtureManifest $modern
Assert-HpvrReject {Read-HpvrReleaseManifest $kit} 'Old APK cannot claim prepared scenes'
$modern.versionCode=43
$modern.preparedSceneVersion=2
Write-FixtureManifest $modern
Assert-HpvrReject {Read-HpvrReleaseManifest $kit} 'Unknown scene format rejected'
$modern.preparedSceneVersion=1
$modern.files=$entries
Write-FixtureManifest $modern
Assert-HpvrReject {Read-HpvrReleaseManifest $kit} 'Claimed scene capability requires hashed preparation tool'
$modern.files=@($entries)+@($sceneToolEntry)
Write-FixtureManifest $modern
[IO.File]::WriteAllBytes($sceneToolPath,[byte[]](99))
Assert-HpvrReject {Get-HpvrScenePreparationTool $kit $modern} 'Preparation tool changed after manifest verification is rejected'
[IO.File]::WriteAllBytes($sceneToolPath,[byte[]](77,90,10,20))
$script:sceneCalls=[Collections.Generic.List[object]]::new()
$script:sceneMode='valid'
function Invoke-HpvrChecked([string]$Executable,[string[]]$Arguments,[string]$Label){
    $script:sceneCalls.Add([pscustomobject]@{Executable=$Executable;Arguments=$Arguments;Label=$Label})
    if ($script:sceneMode -eq 'failure') { throw 'Synthetic preparation failure' }
    $isVerify=$Arguments -contains '--verify'
    if ($isVerify) {
        if ($script:sceneMode -eq 'verify-failure') { throw 'Synthetic cache verification failure' }
        'SCENE_VERIFY=PASS';return
    }
    $map=[int]$Arguments[4]
    $length=if($script:sceneMode -eq 'short'){8}else{64}
    [IO.File]::WriteAllBytes((Join-Path $Arguments[2] "map-$map.hpvc"),(New-Object byte[] $length))
    if($script:sceneMode -eq 'extra'){[IO.File]::WriteAllBytes((Join-Path $Arguments[2] 'unexpected.hpvc'),[byte[]](1))}
    'SCENE_PREPARE=PASS'
}
function New-HpvrSceneFixture([bool]$Challenge=$true, [bool]$Broom=$false, [bool]$Charms=$false){
    $run=Join-Path $fixture ('private-'+[Guid]::NewGuid().ToString('N'))
    $stage=Join-Path $run 'HP'
    New-Item -ItemType Directory -Path (Join-Path $stage 'Maps') | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $stage 'Maps/Lev_Tut1.unr'),[byte[]](1,2,3))
    if($Challenge){[IO.File]::WriteAllBytes((Join-Path $stage 'Maps/Lev_Tut1b.unr'),[byte[]](4,5,6))}
    if($Broom){[IO.File]::WriteAllBytes((Join-Path $stage 'Maps/Lev_Tut2.unr'),[byte[]](7,8,9))}
    if($Charms){[IO.File]::WriteAllBytes((Join-Path $stage 'Maps/Lev_Tut3.unr'),[byte[]](10,11,12))}
    return [pscustomobject]@{Run=$run;Stage=$stage}
}
$testScene=New-HpvrSceneFixture $true $true $true
$prepared=@(Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run $true $true $true)
Assert-HpvrTest ($prepared.Count -eq 4 -and $prepared[3].path -eq 'Cache/Scenes/map-3.hpvc' -and $script:sceneCalls.Count -eq 8) 'All four maps are prepared and verified'
$script:sceneCalls.Clear()
$testScene=New-HpvrSceneFixture
$prepared=@(Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run)
Assert-HpvrTest ($prepared.Count -eq 1 -and $prepared[0].path -eq 'Cache/Scenes/map-0.hpvc') 'Default installer prepares only map zero despite map one being present'
Assert-HpvrTest ($script:sceneCalls.Count -eq 2 -and $script:sceneCalls[0].Arguments.Count -eq 5 -and $script:sceneCalls[1].Arguments[-1] -eq '--verify') 'Every prepared map receives independent verification'
Assert-HpvrTest ($script:sceneCalls[0].Arguments[0] -eq $testScene.Stage -and $script:sceneCalls[0].Arguments[1] -eq '--output' -and $script:sceneCalls[0].Arguments[3] -eq '--map') 'Preparation CLI uses staged owned copy and explicit output/map'
Assert-HpvrTest ($script:sceneCalls[0].Arguments[2] -eq (Join-Path $testScene.Run 'PreparedScenes') -and -not (Test-HpvrWithin $script:sceneCalls[0].Arguments[2] $testScene.Stage)) 'Native preparation writes only to private sibling outside game-input tree'
Assert-HpvrTest ($prepared[0].bytes -eq 64 -and $prepared[0].sha256 -eq (Get-FileHash -LiteralPath (Join-Path $testScene.Stage $prepared[0].path) -Algorithm SHA256).Hash) 'Private import manifest hashes exact prepared bytes'
Assert-HpvrTest ($prepared[0].sha256 -eq (Get-FileHash -LiteralPath (Join-Path $testScene.Run 'PreparedScenes/map-0.hpvc') -Algorithm SHA256).Hash) 'Transferable cache is byte-identical to verified native output'
Assert-HpvrTest (-not (Test-Path -LiteralPath (Join-Path $owned 'Cache')) -and -not (Test-Path -LiteralPath (Join-Path $kit 'Cache'))) 'Preparation never writes original installation or release'
$script:sceneCalls.Clear()
$testScene=New-HpvrSceneFixture
$prepared=@(Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run $true)
Assert-HpvrTest ($prepared.Count -eq 2 -and $prepared[1].path -eq 'Cache/Scenes/map-1.hpvc' -and $script:sceneCalls.Count -eq 4) 'IncludeChallenge prepares and verifies both exact map files'
$script:sceneCalls.Clear()
$testScene=New-HpvrSceneFixture
$prepared=@(Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run ($alphaMaps -contains 1))
Assert-HpvrTest ($prepared.Count -eq 2 -and $script:sceneCalls.Count -eq 4 -and $prepared[0].path -eq 'Cache/Scenes/map-0.hpvc' -and $prepared[1].path -eq 'Cache/Scenes/map-1.hpvc') 'Alpha manifest produces exactly two prepared scenes without CLI opt-in'
foreach ($map in @(0,1)) {
    $prepareCall=$script:sceneCalls[2*$map]
    $verifyCall=$script:sceneCalls[2*$map+1]
    Assert-HpvrTest ($prepareCall.Arguments[4] -eq [string]$map -and $verifyCall.Arguments[4] -eq [string]$map -and $verifyCall.Arguments[-1] -eq '--verify') ("Alpha map $map is independently prepared and verified")
    Assert-HpvrTest ($prepared[$map].sha256 -eq (Get-FileHash -LiteralPath (Join-Path $testScene.Stage $prepared[$map].path) -Algorithm SHA256).Hash) ("Alpha map $map transfer entry matches prepared bytes")
}
$script:sceneCalls.Clear()
$testScene=New-HpvrSceneFixture $true $true
$prepared=@(Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run $true $true)
Assert-HpvrTest ($prepared.Count -eq 3 -and $script:sceneCalls.Count -eq 6 -and $prepared[2].path -eq 'Cache/Scenes/map-2.hpvc') 'Three-map release prepares and verifies exactly three caches'
foreach ($map in @(0,1,2)) {
    Assert-HpvrTest ($script:sceneCalls[2*$map].Arguments[4] -eq [string]$map -and $script:sceneCalls[2*$map+1].Arguments[-1] -eq '--verify') ("Three-map release independently verifies map $map")
    Assert-HpvrTest ($prepared[$map].sha256 -eq (Get-FileHash -LiteralPath (Join-Path $testScene.Stage $prepared[$map].path) -Algorithm SHA256).Hash) ("Three-map cache $map matches transfer entry")
}
$script:sceneCalls.Clear()
$testScene=New-HpvrSceneFixture
$prepared=@(Invoke-HpvrScenePreparation '' $testScene.Stage $owned $kit $testScene.Run)
Assert-HpvrTest ($prepared.Count -eq 0 -and $script:sceneCalls.Count -eq 0 -and -not (Test-Path -LiteralPath (Join-Path $testScene.Stage 'Cache'))) 'Legacy or explicit skipped preparation performs no cache work'
Assert-HpvrReject {Invoke-HpvrScenePreparation $sceneToolPath $owned $owned $kit $fixture} 'Original installation cannot be preparation target'
Assert-HpvrReject {Invoke-HpvrScenePreparation $sceneToolPath (Join-Path $kit 'HP') $owned $kit $kit} 'Release directory cannot be preparation target'
Assert-HpvrReject {Invoke-HpvrScenePreparation $sceneToolPath (Join-Path $fixture 'other') $owned $kit $testScene.Run} 'Arbitrary non-staging output rejected'
$testScene=New-HpvrSceneFixture $false
Assert-HpvrReject {Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run $true} 'Missing requested challenge fails before native preparation'
Assert-HpvrTest ($script:sceneCalls.Count -eq 0) 'Unsafe targets and missing map cannot invoke helper'
$testScene=New-HpvrSceneFixture $true
Assert-HpvrReject {Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run $true $true} 'Missing requested broom map fails before native preparation'
Assert-HpvrTest ($script:sceneCalls.Count -eq 0) 'Missing broom map cannot invoke preparation helper'
foreach($mode in @('short','failure','verify-failure','extra')){
    $script:sceneMode=$mode
    $testScene=New-HpvrSceneFixture
    Assert-HpvrReject {Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run} ("Reject prepared scene failure: $mode")
    if ($mode -eq 'verify-failure') {
        Assert-HpvrTest (-not (Test-Path -LiteralPath (Join-Path $testScene.Stage 'Cache/Scenes/map-0.hpvc'))) 'Failed native verification cannot copy cache into transferable game tree'
    }
}
$script:sceneMode='valid'
$testScene=New-HpvrSceneFixture
$cacheFolder=Join-Path $testScene.Stage 'Cache/Scenes'
New-Item -ItemType Directory -Path $cacheFolder | Out-Null
[IO.File]::WriteAllBytes((Join-Path $cacheFolder 'map-1.hpvc'),(New-Object byte[] 64))
$script:sceneCalls.Clear()
Assert-HpvrReject {Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run} 'Demo preparation cannot accidentally import a pre-existing second-map cache'
Assert-HpvrTest ($script:sceneCalls.Count -eq 0) 'Unexpected cache inventory rejected before helper execution'
$testScene=New-HpvrSceneFixture
$cacheParent=Join-Path $testScene.Stage 'Cache'
New-Item -ItemType Directory -Path $cacheParent | Out-Null
New-Item -ItemType Junction -Path (Join-Path $cacheParent 'Scenes') -Target $owned | Out-Null
Assert-HpvrReject {Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run} 'Cache junction cannot redirect preparation into original data'
Assert-HpvrTest ($script:sceneCalls.Count -eq 0 -and -not (Test-Path -LiteralPath (Join-Path $owned 'map-0.hpvc'))) 'Rejected junction performs no native call or write to owned data'
$testScene=New-HpvrSceneFixture
New-Item -ItemType Junction -Path (Join-Path $testScene.Run 'PreparedScenes') -Target $owned | Out-Null
Assert-HpvrReject {Invoke-HpvrScenePreparation $sceneToolPath $testScene.Stage $owned $kit $testScene.Run} 'Native output junction cannot redirect preparation into original installation'
Assert-HpvrTest ($script:sceneCalls.Count -eq 0) 'Native output junction rejected before helper execution'
# The main installer must use one resolved selection for package closure,
# dialogue/audio enumeration, and scene preparation. No native tool is run.
$installerSource=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'INSTALL-HPVR.ps1') -Raw
Assert-HpvrTest ($installerSource.Contains('$selectedMapIds = @(Get-HpvrReleaseMapIds $release -IncludeChallenge:$IncludeChallenge)') -and $installerSource.Contains('$withChallenge = $selectedMapIds -contains 1')) 'Main installer resolves manifest and legacy switch once'
Assert-HpvrTest ($installerSource.Contains('$withBroom = $selectedMapIds -contains 2')) 'Main installer resolves broom map from release metadata'
Assert-HpvrTest ($installerSource.Contains('Get-HpvrDependencySet $ownedRoot $graphProbe $withChallenge $withBroom')) 'Package import uses resolved map selection'
Assert-HpvrTest ($installerSource.Contains('Get-HpvrFrontendAudioPlan $stagedGame $frontendProbe $encodedRoot $privateRun $withChallenge $withBroom')) 'Speech and music enumeration uses the same resolved map selection'
Assert-HpvrTest ($installerSource.Contains('Invoke-HpvrScenePreparation $scenePreparationTool $stagedGame $ownedRoot $bundleRoot $privateRun $withChallenge $withBroom')) 'Prepared scenes use the same resolved map selection'
# Evaluate only the packager's mapIds value expression, never its executable
# script body. In particular, PowerShell must not unwrap a single-map array.
$packagePath=Join-Path $PSScriptRoot '../workspace/PACKAGE-QUEST-PLAYER.ps1'
$packageTokens=$null
$packageErrors=$null
$packageAst=[Management.Automation.Language.Parser]::ParseFile($packagePath,[ref]$packageTokens,[ref]$packageErrors)
Assert-HpvrTest ($packageErrors.Count -eq 0) 'Player packager parses without executing it'
$mapTables=@($packageAst.FindAll({param($node)
    $node -is [Management.Automation.Language.HashtableAst] -and
    @($node.KeyValuePairs | Where-Object {$_.Item1.Extent.Text -eq 'mapIds'}).Count -eq 1
},$true))
Assert-HpvrTest ($mapTables.Count -eq 1) 'Packager has one explicit release map declaration'
$mapValue=@($mapTables[0].KeyValuePairs | Where-Object {$_.Item1.Extent.Text -eq 'mapIds'})[0].Item2.Extent.Text
$mapEvaluator=[scriptblock]::Create('[pscustomobject]@{ mapIds = ' + $mapValue + ' }')
foreach($code in @(37,54,56,57,71)) {
    $hpvrMetadata=[pscustomobject]@{versionCode=$code}
    $roundtrip=(& $mapEvaluator | ConvertTo-Json -Depth 4) | ConvertFrom-Json
    $expectedCount=if($code -eq 37){1}elseif($code -ge 71){4}elseif($code -ge 57){3}else{2}
    Assert-HpvrTest ($roundtrip.mapIds -is [Array] -and $roundtrip.mapIds.Count -eq $expectedCount -and $roundtrip.mapIds[0] -eq 0) ("Packager C$code map declaration survives JSON as an array")
    if($code -eq 54){Assert-HpvrTest ($roundtrip.mapIds[1] -eq 1) 'Packager alpha includes challenge map one'}
    if($code -eq 57){Assert-HpvrTest ($roundtrip.mapIds[1] -eq 1 -and $roundtrip.mapIds[2] -eq 2) 'Packager new alpha includes challenge and broom maps'}
}
# Audio uses synthetic probe output; no original clips or native programs are used.
$script:audioCalls = [Collections.Generic.List[object]]::new()
$script:audioMode = 'valid'
function Invoke-HpvrChecked([string]$Executable, [string[]]$Arguments, [string]$Label) {
    $script:audioCalls.Add([pscustomobject]@{Arguments=$Arguments; Label=$Label})
    $passRoot = $Arguments[1]
    New-Item -ItemType Directory -Path $passRoot -Force | Out-Null
    $map = if ($Arguments -contains '--map') { [int]$Arguments[-1] } else { -1 }
    $rows = [Collections.Generic.List[string]]::new()
    $count = if ($script:audioMode -eq 'truncated') { 2 } else { 20 }
    for ($index = 0; $index -lt $count; ++$index) {
        $key = 'Shared' + $index + '.0123abcd.s16'
        $rows.Add($key + "`t1")
        if ($script:audioMode -ne 'missing-source' -or $index -ne 0) {
            $bytes = if ($script:audioMode -eq 'conflicting-source' -and $map -eq 1 -and $index -eq 0) { [byte[]](9,8,7) } else { [byte[]](1,2,3) }
            [IO.File]::WriteAllBytes((Join-Path $passRoot ($key + '.mp2')), $bytes)
        }
    }
    if ($script:audioMode -eq 'duplicate') { $rows.Add($rows[0]) }
    if ($script:audioMode -eq 'unsafe-name') { $rows.Add("../escape.0123abcd.s16`t1") }
    if ($script:audioMode -eq 'channel-mismatch') { $rows.Add("Invalid.0123abcd.stereo.s16`t1") }
    $unique = 'Map' + ($map + 1) + '.0123abcd.s16'
    $rows.Add($unique + "`t1")
    [IO.File]::WriteAllBytes((Join-Path $passRoot ($unique + '.mp2')), [byte[]](4,5,6))
    [IO.File]::WriteAllLines((Join-Path $passRoot 'audio-plan.tsv'), $rows)
    'FRONTEND=PASS'
}
function New-HpvrAudioFixture {
    $run = Join-Path $fixture ('audio-' + [Guid]::NewGuid().ToString('N'))
    $encoded = Join-Path $run 'encoded-private'
    New-Item -ItemType Directory -Path $encoded | Out-Null
    return [pscustomobject]@{Run=$run; Encoded=$encoded}
}
foreach ($challenge in @($false, $true)) {
    $script:audioCalls.Clear()
    $audioFixture = New-HpvrAudioFixture
    $plan = Get-HpvrFrontendAudioPlan $owned 'synthetic-audio.exe' $audioFixture.Encoded $audioFixture.Run $challenge
    Assert-HpvrTest ($plan.Count -eq 21 -and $script:audioCalls.Count -eq 1) 'Legacy audio extraction remains a single pass'
    Assert-HpvrTest (($script:audioCalls[0].Arguments -contains '--challenge') -eq $challenge -and $script:audioCalls[0].Arguments -notcontains '--map') 'Legacy probe keeps compatible challenge argument'
}
$script:audioCalls.Clear()
$audioFixture = New-HpvrAudioFixture
$plan = Get-HpvrFrontendAudioPlan $owned 'synthetic-audio.exe' $audioFixture.Encoded $audioFixture.Run $true $true
Assert-HpvrTest ($plan.Count -eq 23 -and $script:audioCalls.Count -eq 3) 'Three-map audio merges distinct clips and deduplicates shared originals'
foreach ($map in @(0,1,2)) {
    Assert-HpvrTest ($script:audioCalls[$map].Arguments[-2] -eq '--map' -and $script:audioCalls[$map].Arguments[-1] -eq [string]$map) ("Map $map audio is enumerated explicitly")
    $key = 'Map' + ($map + 1) + '.0123abcd.s16'
    Assert-HpvrTest ($plan.ContainsKey($key) -and (Test-Path -LiteralPath (Join-Path $audioFixture.Encoded ($key + '.mp2')))) ("Map $map unique speech reaches the shared decoding input")
}
foreach ($mode in @('truncated', 'duplicate', 'unsafe-name', 'channel-mismatch', 'missing-source', 'conflicting-source')) {
    $script:audioMode = $mode
    $audioFixture = New-HpvrAudioFixture
    Assert-HpvrReject { Get-HpvrFrontendAudioPlan $owned 'synthetic-audio.exe' $audioFixture.Encoded $audioFixture.Run $true $true } ("Invalid audio union fails closed: $mode")
}
Assert-HpvrReject { Get-HpvrFrontendAudioPlan $owned 'synthetic-audio.exe' $audioFixture.Encoded $audioFixture.Run $false $true } 'Broom audio cannot bypass the preceding map'
$script:audioMode = 'valid'
$script:audioCalls.Clear()
$audioFixture = New-HpvrAudioFixture
$plan = Get-HpvrFrontendAudioPlan $owned 'synthetic-audio.exe' $audioFixture.Encoded $audioFixture.Run $true $true $true
Assert-HpvrTest ($plan.Count -eq 24 -and $script:audioCalls.Count -eq 4 -and $script:audioCalls[3].Arguments[-1] -eq '3') 'Charms audio is included and deduplicated'

$permissionCommands = @(Get-HpvrDataPermissionCommands @('system/HPBase.u','Cache/Audio/test.s16','Cache/Scenes/map-3.hpvc'))
$permissionText = $permissionCommands -join "`n"
Assert-HpvrTest ($permissionCommands.Count -eq 2) 'Permission repair batches directories before files'
foreach ($suffix in @('/HP', '/HP/system', '/HP/Cache', '/HP/Cache/Audio', '/HP/Cache/Scenes')) {
    Assert-HpvrTest ($permissionCommands[0].Contains("$suffix'")) "Permission repair includes parent $suffix"
}
Assert-HpvrTest ($permissionCommands[0].Contains('chmod o+rx') -and $permissionCommands[1].Contains('chmod o+r')) 'Minimal read and directory traverse rights'
Assert-HpvrTest ($permissionText.Contains('stat -c %a') -and $permissionText.Contains('DATA_PERMISSION_FAILURE')) 'Modes are read back, not inferred from successful chmod'
Assert-HpvrTest ((@(Get-HpvrDataPermissionCommands @('Maps/test.unr') $true) -join '') -notmatch 'stat -c') 'Current APK uses actual application reads on masked storage'
Assert-HpvrTest ($installerSource.Contains('HPVR_DATA_ACCESS=PASS files=') -and $installerSource.Contains('Broadcast completed: result=-1')) 'Application probe must explicitly succeed'
Assert-HpvrTest ($permissionText.Contains('test ! -L') -and -not $permissionText.Contains('chmod -R') -and -not $permissionText.Contains('777')) 'No recursive or world-write permission changes'
foreach ($unsafe in @('saves/slot.sav','../Maps/map.unr',"Maps/bad'file",'/sdcard/other','Cache/../outside')) {
    Assert-HpvrReject { Get-HpvrDataPermissionCommands @($unsafe) } 'Unsafe permission target rejected'
}
Assert-HpvrReject { Get-HpvrDataPermissionCommands @() } 'Empty permission import rejected'
$script:permissionCalls = 0
function Invoke-HpvrChecked([string]$Executable,[string[]]$Arguments,[string]$Label) {
    ++$script:permissionCalls
    throw 'Synthetic chmod/readback failure'
}
Assert-HpvrReject { Set-HpvrDataPermissions 'synthetic-adb' @('-s','test') @('Maps/test.unr') } 'Permission failure prevents success'
Assert-HpvrTest ($script:permissionCalls -eq 1) 'Stop at first failed permission batch'
Assert-HpvrTest ($installerSource.IndexOf('Set-HpvrDataPermissions $adb') -lt $installerSource.IndexOf('INSTALL=PASS')) 'Access verification precedes installation success'
Write-Output "PLAYER_INSTALL_TESTS=PASS checks=$script:checks device_actions=0 native_tools_executed=0 fixtures=$fixture"
