#requires -Version 5.1
[CmdletBinding()]
param([string]$FrontendProbe)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$hpvrCharmsTestRepository=Split-Path -Parent $PSScriptRoot
. (Join-Path $hpvrCharmsTestRepository 'tools/workspace/PREPARE-QUEST-CHARMS.ps1') -LibraryOnly
$script:hpvrCharmsChecks=0
function Test-Charms([bool]$Pass,[string]$Message) {
    if (-not $Pass) { throw "Charms preparation test failed: $Message" }
    ++$script:hpvrCharmsChecks
}
function Reject-Charms([scriptblock]$Action,[string]$Message) {
    $rejected=$false
    try { & $Action | Out-Null } catch { $rejected=$true }
    Test-Charms $rejected $Message
}
$fixture=Join-Path $hpvrCharmsTestRepository ('local/charms-helper-tests-'+[Guid]::NewGuid().ToString('N'))
$owned=Join-Path $fixture 'Owned'
New-Item -ItemType Directory -Path (Join-Path $owned 'Maps'),(Join-Path $owned 'system'),(Join-Path $owned 'Textures') -Force | Out-Null
foreach ($name in @('Maps/Lev_Tut1.unr','Maps/Lev_Tut1b.unr','Maps/Lev_Tut2.unr','Maps/Lev_Tut3.unr','system/Test.u','Textures/Test.utx')) {
    [IO.File]::WriteAllBytes((Join-Path $owned $name),[byte[]](1,2,3))
}
function Get-HpvrDependencySet([string]$Root,[string]$GraphProbe,[bool]$WithChallenge) {
    Test-Charms $WithChallenge 'introduction and challenge remain in the closure'
    return [pscustomobject]@{
        Inputs=@((Get-HpvrOwnedInput $Root (Join-Path $Root 'Maps/Lev_Tut1.unr')),
                 (Get-HpvrOwnedInput $Root (Join-Path $Root 'Maps/Lev_Tut1b.unr')))
        Report=@('fixture base closure')
    }
}
$script:hpvrCharmsGraphMode='complete'
$script:hpvrCharmsScanned=@()
function Invoke-HpvrChecked([string]$Executable,[string[]]$Arguments,[string]$Label) {
    Test-Charms ($Executable -eq 'fixture-probe' -and $Arguments.Count -eq 2) 'bounded dependency scan arguments'
    $map=[IO.Path]::GetFileName($Arguments[1])
    Test-Charms ($map -in @('Lev_Tut2.unr','Lev_Tut3.unr')) 'only the two explicit later maps are scanned'
    $script:hpvrCharmsScanned+=,$map
    $names=if($map -ne 'Lev_Tut3.unr' -or $script:hpvrCharmsGraphMode -eq 'complete') {@(('Maps/'+$map),'system/Test.u','Textures/Test.utx')}
        elseif($script:hpvrCharmsGraphMode -eq 'missing-map') {@('Maps/Lev_Tut2.unr','system/Test.u','Textures/Test.utx')}
        elseif($script:hpvrCharmsGraphMode -eq 'escape') {@('../escape.u','system/Test.u','Textures/Test.utx')}
        else {@('Maps/Lev_Tut3.unr')}
    foreach ($name in $names) { 'package=Fixture kind=data path='+[IO.Path]::GetFullPath((Join-Path $Arguments[0] $name))+' version=69 direct_dependencies=0 native_companion=0' }
}
$closure=Get-HpvrBroomDependencies $owned 'fixture-probe' 3
Test-Charms ($closure.Inputs.Count -eq 6 -and $closure.Inputs.Relative -contains 'Maps/Lev_Tut3.unr') 'four-map dependency closure includes the owned charms map'
Test-Charms (($script:hpvrCharmsScanned -join ',') -eq 'Lev_Tut2.unr,Lev_Tut3.unr') 'broom closure is preserved before adding charms'
foreach ($mode in @('short','missing-map','escape')) {
    $script:hpvrCharmsGraphMode=$mode
    Reject-Charms { Get-HpvrBroomDependencies $owned 'fixture-probe' 3 } 'incomplete or escaped charms dependency rejected'
}
Reject-Charms { Get-HpvrBroomDependencies $owned 'fixture-probe' 4 } 'unimplemented preparation map rejected'
Reject-Charms { Invoke-HpvrBroomPreparation $owned (Join-Path $hpvrCharmsTestRepository 'artifacts/charms-test') 'unused' 'unused' 3 } 'owned data cannot enter release artifacts'
Reject-Charms { Invoke-HpvrBroomPreparation $owned (Join-Path ([IO.Path]::GetTempPath()) 'hpvr-charms-outside-local') 'unused' 'unused' 3 } 'charms output outside ignored local is rejected'
Test-Charms ($hpvrCharmsArguments.MapId -eq 3) 'charms wrapper selects map3'
if (-not [string]::IsNullOrWhiteSpace($FrontendProbe)) {
    foreach ($arguments in @(@('unused','unused','--map','4'),@('unused','unused','--map','-1'),
                             @('unused','unused','--map','03'),@('unused','unused','--map','3','extra'))) {
        $savedPreference=$ErrorActionPreference
        try { $ErrorActionPreference='Continue'; $null=& $FrontendProbe @arguments 2>&1; $exitCode=$LASTEXITCODE }
        finally { $ErrorActionPreference=$savedPreference }
        Test-Charms ($exitCode -eq 2) 'frontend probe rejects unsupported and malformed map selectors'
    }
}
Write-Host "CHARMS_PREPARATION_TESTS=PASS checks=$script:hpvrCharmsChecks fixture=$fixture"
