[CmdletBinding()]
param(
    [string]$DataRoot = 'C:\Program Files\HP',
    [string]$Ffmpeg = 'C:\Program Files\Virtual Desktop Streamer\ffmpeg.exe'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$probe = Join-Path $repo 'build\src\wand\Release\hpvr_hp1_sound_probe.exe'
$cache = Join-Path $repo 'local\quest-owned-audio'
foreach ($required in @($probe, $Ffmpeg, (Join-Path $DataRoot 'Sounds\AllDialog.uax'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing input: $required" }
}
New-Item -ItemType Directory -Path $cache -Force | Out-Null
$clips = @(
    @('AllDialog.uax', '111DumbledoreInfo1'),
    @('AllDialog.uax', '111DumbledoreInfo2'),
    @('AllDialog.uax', '111DumbledoreInfo3'),
    @('AllDialog.uax', '111DumbledoreInfo4'),
    @('AllDialog.uax', 'Dumbledore_01'),
    @('Magic_sfx.uax', 'spell_tracing_loop'),
    @('Magic_sfx.uax', 'wand_ready_loop'),
    @('Magic_sfx.uax', 'spell_cast'),
    @('Magic_sfx.uax', 'flipendo_no')
)
foreach ($clip in $clips) {
    $package = Join-Path $DataRoot ('Sounds\' + $clip[0])
    $encoded = Join-Path $cache ($clip[1] + '.mp2')
    $report = & $probe $package --export-mpeg $clip[1] $encoded
    if ($LASTEXITCODE -ne 0) { throw "Owned clip export failed: $($clip[1])" }
    $nameLine = @($report | Where-Object { $_ -like 'cache_name=*' })
    if ($nameLine.Count -ne 1) { throw 'Missing encoded-source fingerprint' }
    $output = Join-Path $cache $nameLine[0].Substring(11)
    & $Ffmpeg -hide_banner -loglevel error -y -i $encoded -f s16le -acodec pcm_s16le -ac 1 -ar 48000 $output
    if ($LASTEXITCODE -ne 0) { throw "PCM conversion failed: $($clip[1])" }
    $size = (Get-Item -LiteralPath $output).Length
    if ($size -le 960 -or $size -gt 17280000 -or $size % 2 -ne 0) { throw "Invalid PCM size: $size" }
    $samples = [System.IO.File]::ReadAllBytes($output)
    if (-not ($samples | Where-Object { $_ -ne 0 } | Select-Object -First 1)) { throw 'Decoded clip is silent' }
    [pscustomobject]@{ Clip = $clip[1]; Seconds = [math]::Round($size / 96000.0, 3); File = $output; SHA256 = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash }
}
# Private derived owned assets remain in ignored local/, never inside an APK
# or source kit. Installation is a separate explicit workflow step.
