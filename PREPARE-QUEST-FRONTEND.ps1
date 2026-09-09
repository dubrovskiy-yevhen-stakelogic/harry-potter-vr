[CmdletBinding()]
param([string]$DataRoot='C:\Program Files\HP')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $MyInvocation.MyCommand.Path
$out=Join-Path $repo 'local\c26-frontend'
$cache=Join-Path $repo 'local\quest-owned-audio'
$probe=Join-Path $repo 'build\quest-host-tests\src\quest\Release\hpvr_quest_frontend_probe.exe'
& $probe $DataRoot $out
if($LASTEXITCODE -ne 0){throw 'Frontend owned-data extraction failed'}
foreach($row in Get-Content -LiteralPath (Join-Path $out 'audio-plan.tsv')){
    $fields=$row.Split("`t")
    if($fields.Count -ne 2 -or $fields[0] -notmatch '^[A-Za-z0-9_]+\.[0-9a-f]{8}(\.stereo)?\.s16$'){throw 'Invalid cache plan'}
    $input=Join-Path $out ($fields[0]+'.mp2')
    $output=Join-Path $cache $fields[0]
    & 'C:\Program Files\Virtual Desktop Streamer\ffmpeg.exe' -hide_banner -loglevel error -y -i $input -f s16le -acodec pcm_s16le -ac $fields[1] -ar 48000 $output
    if($LASTEXITCODE -ne 0){throw "Audio decode failed: $input"}
    $size=(Get-Item -LiteralPath $output).Length
    if($size -lt 1920 -or $size -gt 57600000){throw "Invalid PCM size: $size"}
    Write-Output "$($fields[0]) seconds=$([math]::Round($size/(48000*2*[int]$fields[1]),2))"
}
