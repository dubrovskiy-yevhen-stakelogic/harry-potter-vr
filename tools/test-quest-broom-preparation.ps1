#requires -Version 5.1
[CmdletBinding()]
param([string]$FrontendProbe)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$hpvrTestRepository=Split-Path -Parent $PSScriptRoot
. (Join-Path $hpvrTestRepository 'tools/workspace/PREPARE-QUEST-BROOM.ps1') -LibraryOnly
$hpvrTestChecks=0
function Test-Broom([bool]$Pass,[string]$Message) {
    if (-not $Pass) { throw "Broom preparation test failed: $Message" }
    ++$script:hpvrTestChecks
}
function Reject-Broom([scriptblock]$Action,[string]$Message) {
    $rejected=$false
    try { & $Action | Out-Null } catch { $rejected=$true }
    Test-Broom $rejected $Message
}
$fixture=Join-Path $hpvrTestRepository ('local/broom-helper-tests-'+[Guid]::NewGuid().ToString('N'))
$owned=Join-Path $fixture 'Owned'
$private=Join-Path $fixture 'Private'
New-Item -ItemType Directory -Path (Join-Path $owned 'Maps'),(Join-Path $owned 'system'),(Join-Path $owned 'Textures') -Force | Out-Null
foreach ($name in @('Maps/Lev_Tut1.unr','Maps/Lev_Tut1b.unr','Maps/Lev_Tut2.unr','system/Test.u','Textures/Test.utx')) {
    [IO.File]::WriteAllBytes((Join-Path $owned $name),[byte[]](1,2,3))
}
Assert-HpvrBroomOutput $private $owned $hpvrTestRepository
Test-Broom $true 'isolated ignored local output accepted'
Reject-Broom { Assert-HpvrBroomOutput (Join-Path $owned 'output') $owned $hpvrTestRepository } 'output inside owned installation rejected'
Reject-Broom { Assert-HpvrBroomOutput $fixture $owned $hpvrTestRepository } 'output containing owned installation rejected'
Reject-Broom { Assert-HpvrBroomOutput $hpvrTestRepository $owned $hpvrTestRepository } 'repository root rejected'
Reject-Broom { Assert-HpvrBroomOutput (Join-Path $hpvrTestRepository 'artifacts/broom') $owned $hpvrTestRepository } 'release artifact directory cannot hold private data'
Reject-Broom { Assert-HpvrBroomOutput ([IO.Path]::GetPathRoot($fixture)) $owned $hpvrTestRepository } 'drive root rejected'
function Get-HpvrDependencySet([string]$Root,[string]$GraphProbe,[bool]$WithChallenge) {
    Test-Broom $WithChallenge 'both existing maps remain in dependency closure'
    return [pscustomobject]@{
        Inputs=@((Get-HpvrOwnedInput $Root (Join-Path $Root 'Maps/Lev_Tut1.unr')),
                 (Get-HpvrOwnedInput $Root (Join-Path $Root 'Maps/Lev_Tut1b.unr')))
        Report=@('fixture base closure')
    }
}
$script:hpvrTestGraphMode='complete'
function Invoke-HpvrChecked([string]$Executable,[string[]]$Arguments,[string]$Label) {
    Test-Broom ($Executable -eq 'fixture-probe' -and $Arguments.Count -eq 2 -and $Arguments[1] -eq (Join-Path $Arguments[0] 'Maps/Lev_Tut2.unr')) 'map2 dependency scan uses exact owned path'
    $names=if($script:hpvrTestGraphMode -eq 'complete') {@('Maps/Lev_Tut2.unr','system/Test.u','Textures/Test.utx')}
        elseif($script:hpvrTestGraphMode -eq 'missing-map') {@('Maps/Lev_Tut1.unr','system/Test.u','Textures/Test.utx')}
        elseif($script:hpvrTestGraphMode -eq 'escape') {@('../escape.u','system/Test.u','Textures/Test.utx')}
        else {@('Maps/Lev_Tut2.unr')}
    foreach ($name in $names) { 'package=Fixture kind=data path='+[IO.Path]::GetFullPath((Join-Path $Arguments[0] $name))+' version=69 direct_dependencies=0 native_companion=0' }
}
$closure=Get-HpvrBroomDependencies $owned 'fixture-probe'
Test-Broom ($closure.Inputs.Count -eq 5 -and $closure.Inputs.Relative -contains 'Maps/Lev_Tut2.unr' -and $closure.Report.Count -eq 4) 'map2 closure unions all three maps'
$script:hpvrTestGraphMode='short'
Reject-Broom { Get-HpvrBroomDependencies $owned 'fixture-probe' } 'incomplete new-map dependency scan rejected'
$script:hpvrTestGraphMode='missing-map'
Reject-Broom { Get-HpvrBroomDependencies $owned 'fixture-probe' } 'graph omitting the requested map rejected'
$script:hpvrTestGraphMode='escape'
Reject-Broom { Get-HpvrBroomDependencies $owned 'fixture-probe' } 'escaped dependency path rejected'
if (-not [string]::IsNullOrWhiteSpace($FrontendProbe)) {
    foreach ($arguments in @(@('unused','unused','--map','4'),@('unused','unused','--map','-1'),
                             @('unused','unused','--map','01'),@('unused','unused','--map','2','extra'))) {
        $savedPreference=$ErrorActionPreference
        try { $ErrorActionPreference='Continue'; $null=& $FrontendProbe @arguments 2>&1; $exitCode=$LASTEXITCODE }
        finally { $ErrorActionPreference=$savedPreference }
        Test-Broom ($exitCode -eq 2) 'frontend probe rejects malformed and unsupported map selectors'
    }
}
Write-Host "BROOM_PREPARATION_TESTS=PASS checks=$hpvrTestChecks fixture=$fixture"
