#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$DataRoot='C:\Program Files\HP',
    [string]$Ffmpeg='C:\Program Files\Virtual Desktop Streamer\ffmpeg.exe',
    [string]$BuildRoot
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$preparationTimer=[Diagnostics.Stopwatch]::StartNew()
$repo=Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $repo 'tools\release\INSTALL-HPVR.ps1') -LibraryOnly
$ownedRoot=Get-HpvrFullPath $DataRoot
Assert-HpvrNoLinks $ownedRoot
if([string]::IsNullOrWhiteSpace($BuildRoot)){$BuildRoot=Join-Path $repo 'build\quest-host-tests'}
$build=Get-HpvrFullPath $BuildRoot
$graph=Join-Path $build 'src\wand\Release\hpvr_hp1_package_graph.exe'
$sound=Join-Path $build 'src\wand\Release\hpvr_hp1_sound_probe.exe'
$frontend=Join-Path $build 'src\quest\Release\hpvr_quest_frontend_probe.exe'
$scene=Join-Path $build 'src\quest\Release\hpvr_quest_c38_scene_probe.exe'
foreach($tool in @($graph,$sound,$frontend,$scene,$Ffmpeg)){
    if(-not(Test-Path -LiteralPath $tool -PathType Leaf)){throw "Missing tool: $tool. Build the host probes first."}
}
$private=Join-Path $repo 'local\challenge-preparation'
Assert-HpvrNoLinks $private
if(Test-HpvrWithin $private $ownedRoot){throw 'Private preparation must remain outside the owned installation.'}
New-Item -ItemType Directory -Path $private -Force | Out-Null

# Use the same safe, package-only dependency resolver as the player importer.
# No package is modified or sent to a device by this developer preparation.
$dependencies=Get-HpvrDependencySet $ownedRoot $graph $true
$dependencies.Report | Set-Content -LiteralPath (Join-Path $private 'dependency-report.txt') -Encoding UTF8
$manifest=[Collections.Generic.List[object]]::new()
foreach($entry in $dependencies.Inputs){
    $manifest.Add([pscustomobject]@{path=$entry.Relative;sha256=(Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash;
        bytes=(Get-Item -LiteralPath $entry.Source).Length})
}
& (Join-Path $repo 'PREPARE-QUEST-FRONTEND.ps1') -DataRoot $ownedRoot -Ffmpeg $Ffmpeg -ProbePath $frontend -IncludeChallenge
$sceneTimer=[Diagnostics.Stopwatch]::StartNew()
Invoke-HpvrChecked $scene @($ownedRoot) 'Challenge scene validation' |
    Set-Content -LiteralPath (Join-Path $private 'scene-report.txt') -Encoding UTF8
$sceneTimer.Stop()
$cache=Join-Path $repo 'local\quest-owned-audio'
$encoded=Join-Path $repo 'local\challenge-frontend'
$supplemental=@{}
Add-HpvrSupplementalAudioPlan $ownedRoot $sound $encoded $supplemental
foreach($key in $supplemental.Keys){
    $file=Join-Path $cache $key
    Assert-HpvrNoLinks $file
    $cached=$false
    if(Test-Path -LiteralPath $file){try{Assert-HpvrPcm $file 1;$cached=$true}catch{}}
    if(-not $cached){
        $temporary=$file+'.pending-'+[Guid]::NewGuid().ToString('N')
        Invoke-HpvrChecked $Ffmpeg @('-nostdin','-hide_banner','-loglevel','error','-y','-i',
            (Join-Path $encoded ($key+'.mp2')),'-f','s16le','-acodec','pcm_s16le','-ac','1','-ar','48000',$temporary) 'Audio decode'
        Assert-HpvrPcm $temporary 1
        Move-Item -LiteralPath $temporary -Destination $file -Force
    }
    $manifest.Add([pscustomobject]@{path='Cache/Audio/'+$key;sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash;
        bytes=(Get-Item -LiteralPath $file).Length})
}
foreach($row in Get-Content -LiteralPath (Join-Path $repo 'local\challenge-frontend\audio-plan.tsv')){
    $key=$row.Split("`t")[0]
    if($key -notmatch '^[A-Za-z0-9_]+\.[0-9a-f]{8}(\.stereo)?\.s16$'){throw 'Invalid cache manifest key'}
    $file=Join-Path $cache $key
    $manifest.Add([pscustomobject]@{path='Cache/Audio/'+$key;sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash;
        bytes=(Get-Item -LiteralPath $file).Length})
}
foreach($entry in $dependencies.Inputs){
    $expected=@($manifest | Where-Object { $_.path -eq $entry.Relative })[0]
    if((Get-FileHash -LiteralPath $entry.Source -Algorithm SHA256).Hash -ne $expected.sha256){throw "Owned input changed during preparation: $($entry.Relative)"}
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $private 'PRIVATE-owned-data-manifest.json') -Encoding UTF8
$preparationTimer.Stop()
Write-Output "CHALLENGE_PREPARE=PASS maps=2 packages=$($dependencies.Inputs.Count) cache=$cache total_seconds=$([math]::Round($preparationTimer.Elapsed.TotalSeconds,2)) scene_seconds=$([math]::Round($sceneTimer.Elapsed.TotalSeconds,2))"
Write-Output 'APK_INSTALL=NOT_PERFORMED DATA_PUSH=NOT_PERFORMED APP_LAUNCH=NOT_PERFORMED'
