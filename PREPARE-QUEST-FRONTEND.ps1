[CmdletBinding()]
param(
    [string]$DataRoot='C:\Program Files\HP',
    [string]$Ffmpeg='C:\Program Files\Virtual Desktop Streamer\ffmpeg.exe',
    [string]$ProbePath,
    [switch]$IncludeChallenge
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $MyInvocation.MyCommand.Path
$withChallenge=[bool]$IncludeChallenge
. (Join-Path $repo 'tools\release\INSTALL-HPVR.ps1') -LibraryOnly
$DataRoot=Get-HpvrFullPath $DataRoot
Assert-HpvrNoLinks $DataRoot
$out=Join-Path $repo $(if($withChallenge){'local\challenge-frontend'}else{'local\c26-frontend'})
$cache=Join-Path $repo 'local\quest-owned-audio'
foreach($path in @($out,$cache)){
    Assert-HpvrNoLinks $path
    if(Test-HpvrWithin $path $DataRoot){throw 'Generated data must remain outside the owned game installation.'}
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}
if([string]::IsNullOrWhiteSpace($ProbePath)){$ProbePath=Join-Path $repo 'build\quest-host-tests\src\quest\Release\hpvr_quest_frontend_probe.exe'}
foreach($path in @($ProbePath,$Ffmpeg)){if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing preparation tool: $path"}}
$arguments=@($DataRoot,$out)
if($withChallenge){$arguments+='--challenge'}
& $ProbePath @arguments
if($LASTEXITCODE -ne 0){throw 'Frontend owned-data extraction failed'}
$prepared=0
foreach($row in Get-Content -LiteralPath (Join-Path $out 'audio-plan.tsv')){
    $fields=$row.Split("`t")
    if($fields.Count -ne 2 -or $fields[0] -notmatch '^[A-Za-z0-9_]+\.[0-9a-f]{8}(\.stereo)?\.s16$' -or
       $fields[1] -notmatch '^[12]$' -or $fields[0].Contains('.stereo.') -ne ($fields[1] -eq '2')){throw 'Invalid cache plan'}
    $encoded=Join-Path $out ($fields[0]+'.mp2')
    $output=Join-Path $cache $fields[0]
    Assert-HpvrNoLinks $encoded
    Assert-HpvrNoLinks $output
    $cached=$false
    if(Test-Path -LiteralPath $output){try{Assert-HpvrPcm $output ([int]$fields[1]);$cached=$true}catch{}}
    if(-not $cached){
        $temporary=$output+'.pending-'+[Guid]::NewGuid().ToString('N')
        & $Ffmpeg -nostdin -hide_banner -loglevel error -y -i $encoded -f s16le -acodec pcm_s16le -ac $fields[1] -ar 48000 $temporary
        if($LASTEXITCODE -ne 0){throw "Audio decode failed: $encoded"}
        Assert-HpvrPcm $temporary ([int]$fields[1])
        Move-Item -LiteralPath $temporary -Destination $output -Force
    }
    $size=(Get-Item -LiteralPath $output).Length
    ++$prepared
    Write-Output "$($fields[0]) seconds=$([math]::Round($size/(48000*2*[int]$fields[1]),2))"
}
Write-Output "FRONTEND_PREPARE=PASS challenge=$withChallenge audio_clips=$prepared cache=$cache device_actions=0"
