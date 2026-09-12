#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GamePath,
    [string]$WorkRoot,
    [string]$HostBuildDirectory = 'build',
    [string]$FfmpegPath,
    [switch]$LibraryOnly
)

# Development preparation only. This never installs an APK, contacts a device,
# launches the game, or changes the released demo's installer and map selection.
$hpvrBroomArguments = @{
    GamePath=$GamePath; WorkRoot=$WorkRoot; HostBuildDirectory=$HostBuildDirectory; FfmpegPath=$FfmpegPath
}
$hpvrBroomLibraryOnly = $LibraryOnly
$hpvrBroomRepository = $PSScriptRoot
. (Join-Path $PSScriptRoot 'tools/release/INSTALL-HPVR.ps1') -LibraryOnly

function Get-HpvrBroomDependencies([string]$Root, [string]$GraphProbe) {
    $base = Get-HpvrDependencySet $Root $GraphProbe $true
    $inputs = @{}
    foreach ($inputFile in $base.Inputs) { $inputs[$inputFile.Relative] = $inputFile }
    $map = Get-HpvrOwnedInput $Root (Join-Path $Root 'Maps/Lev_Tut2.unr')
    $lines = @(Invoke-HpvrChecked $GraphProbe @($Root, $map.Source) 'Owned broomstick map dependency scan')
    $count = 0
    foreach ($line in $lines) {
        if ($line -match '^package=\S+ kind=data path=(.+) version=\d+ direct_dependencies=\d+ native_companion=[01]$') {
            $inputFile = Get-HpvrOwnedInput $Root $Matches[1]
            $inputs[$inputFile.Relative] = $inputFile
            ++$count
        }
    }
    if ($count -lt 3 -or -not $inputs.ContainsKey($map.Relative)) {
        throw 'The owned broomstick map dependency scan is incomplete.'
    }
    return [pscustomobject]@{ Inputs=@($inputs.Values | Sort-Object Relative); Report=@($base.Report) + $lines }
}

function Assert-HpvrBroomOutput([string]$Output, [string]$OwnedRoot, [string]$Repository) {
    $outputPath = Get-HpvrFullPath $Output
    $ownedPath = Get-HpvrFullPath $OwnedRoot
    $repositoryPath = Get-HpvrFullPath $Repository
    Assert-HpvrNoLinks $outputPath
    Assert-HpvrNoLinks $ownedPath
    if ($outputPath -eq [IO.Path]::GetPathRoot($outputPath) -or
        (Test-HpvrWithin $outputPath $ownedPath) -or (Test-HpvrWithin $ownedPath $outputPath) -or
        (Test-HpvrWithin $repositoryPath $outputPath)) {
        throw 'Private output must be a dedicated folder outside the original game tree and must not contain the repository.'
    }
    if ((Test-HpvrWithin $outputPath $repositoryPath) -and
        -not (Test-HpvrWithin $outputPath (Join-Path $repositoryPath 'local'))) {
        throw 'Private output inside this repository must be under ignored local/.'
    }
}

function Invoke-HpvrBroomPreparation([string]$GamePath, [string]$WorkRoot,
                                    [string]$HostBuildDirectory, [string]$FfmpegPath) {
    if ([string]::IsNullOrWhiteSpace($GamePath)) { throw 'Pass -GamePath with your own installed US PC game folder.' }
    $ownedRoot = Get-HpvrFullPath $GamePath
    if (-not (Test-Path -LiteralPath $ownedRoot -PathType Container)) { throw 'GamePath must be an existing folder.' }
    if ([string]::IsNullOrWhiteSpace($WorkRoot)) { $WorkRoot = Join-Path $hpvrBroomRepository 'local/broom-preparation' }
    $privateRoot = Get-HpvrFullPath $WorkRoot
    Assert-HpvrBroomOutput $privateRoot $ownedRoot $hpvrBroomRepository
    $buildRoot = if ([IO.Path]::IsPathRooted($HostBuildDirectory)) { Get-HpvrFullPath $HostBuildDirectory }
        else { Get-HpvrFullPath (Join-Path $hpvrBroomRepository $HostBuildDirectory) }
    $tools = @{
        Graph=Join-Path $buildRoot 'src/wand/Release/hpvr_hp1_package_graph.exe'
        Sound=Join-Path $buildRoot 'src/wand/Release/hpvr_hp1_sound_probe.exe'
        Frontend=Join-Path $buildRoot 'src/quest/Release/hpvr_quest_frontend_probe.exe'
        Prepare=Join-Path $buildRoot 'src/quest/Release/hpvr_quest_prepare_assets.exe'
    }
    $toolHashes = @{}
    foreach ($name in $tools.Keys) {
        Assert-HpvrNoLinks $tools[$name]
        if (-not (Test-Path -LiteralPath $tools[$name] -PathType Leaf)) { throw "Build the matching host tool first: $($tools[$name])" }
        $toolHashes[$name] = (Get-FileHash -LiteralPath $tools[$name] -Algorithm SHA256).Hash
    }
    $assertTool = {
        param([string]$Name)
        Assert-HpvrNoLinks $tools[$Name]
        if ((Get-FileHash -LiteralPath $tools[$Name] -Algorithm SHA256).Hash -ne $toolHashes[$Name]) {
            throw "Host tool changed during preparation: $Name"
        }
    }
    $ffmpeg = Get-HpvrFfmpeg $FfmpegPath @() -NoDownload
    Assert-HpvrNoLinks $ffmpeg
    $ffmpegHash = (Get-FileHash -LiteralPath $ffmpeg -Algorithm SHA256).Hash
    & $assertTool Graph
    $dependencies = Get-HpvrBroomDependencies $ownedRoot $tools.Graph
    $privateRun = Join-Path $privateRoot ('broom-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
    if (Test-Path -LiteralPath $privateRun) { throw 'Private preparation directory already exists.' }
    $stage = Join-Path $privateRun 'HP'
    $audio = Join-Path $stage 'Cache/Audio'
    $encoded = Join-Path $privateRun 'encoded-private'
    $prepared = Join-Path $privateRun 'PreparedScenes'
    $cache = Join-Path $stage 'Cache/Scenes'
    New-Item -ItemType Directory -Path $audio, $encoded, $prepared, $cache -Force | Out-Null
    Write-Host "Private data: $privateRun"
    Write-Host 'Original game data is read-only. This development helper prepares map 2 without changing the public demo.'
    $dependencies.Report | Set-Content -LiteralPath (Join-Path $privateRun 'dependency-report.txt') -Encoding UTF8
    $manifest = [Collections.Generic.List[object]]::new()
    foreach ($inputFile in $dependencies.Inputs) {
        $destination = Join-Path $stage $inputFile.Relative
        Assert-HpvrNoLinks $destination
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
        $hash = (Get-FileHash -LiteralPath $inputFile.Source -Algorithm SHA256).Hash
        Copy-Item -LiteralPath $inputFile.Source -Destination $destination
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash -or
            (Get-FileHash -LiteralPath $inputFile.Source -Algorithm SHA256).Hash -ne $hash) { throw "Owned input changed: $($inputFile.Relative)" }
        $manifest.Add([pscustomobject]@{path=$inputFile.Relative;sha256=$hash;bytes=(Get-Item -LiteralPath $destination).Length})
    }
    $plan = @{}
    foreach ($map in @(0,1,2)) {
        $mapEncoded = Join-Path $privateRun "encoded-map-$map"
        & $assertTool Frontend
        Invoke-HpvrChecked $tools.Frontend @($stage,$mapEncoded,'--map',[string]$map) 'Owned frontend/audio extraction' |
            Set-Content -LiteralPath (Join-Path $privateRun "frontend-$map-report.txt") -Encoding UTF8
        $mapCount = 0
        foreach ($line in (Get-Content -LiteralPath (Join-Path $mapEncoded 'audio-plan.tsv'))) {
            if ($line -notmatch '^([A-Za-z0-9_]+\.[0-9a-f]{8}(\.stereo)?\.s16)\t([12])$') { throw 'Invalid frontend audio conversion plan.' }
            $key=$Matches[1]; $channels=[int]$Matches[3]
            if (($key.Contains('.stereo.')) -ne ($channels -eq 2) -or
                ($plan.ContainsKey($key) -and $plan[$key] -ne $channels)) { throw "Conflicting PCM channel plan: $key" }
            $source=Join-Path $mapEncoded ($key+'.mp2'); $destination=Join-Path $encoded ($key+'.mp2')
            Assert-HpvrNoLinks $source; Assert-HpvrNoLinks $destination
            $hash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
            if (Test-Path -LiteralPath $destination) {
                if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) { throw "Conflicting encoded audio source: $key" }
            } else { Copy-Item -LiteralPath $source -Destination $destination }
            if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) { throw "Encoded audio copy failed: $key" }
            $plan[$key]=$channels; ++$mapCount
        }
        if ($mapCount -lt 20) { throw "Map $map frontend audio plan is incomplete." }
    }
    & $assertTool Sound
    Add-HpvrSupplementalAudioPlan $stage $tools.Sound $encoded $plan
    if ((Get-FileHash -LiteralPath $ffmpeg -Algorithm SHA256).Hash -ne $ffmpegHash) { throw 'FFmpeg changed during preparation.' }
    $index=0
    foreach ($key in ($plan.Keys | Sort-Object)) {
        ++$index
        Write-Progress -Activity 'Preparing original music and speech' -Status "$index / $($plan.Count): $key" -PercentComplete (100.0*$index/$plan.Count)
        $pcm=Join-Path $audio $key
        Invoke-HpvrChecked $ffmpeg @('-nostdin','-hide_banner','-loglevel','error','-n','-i',
            (Join-Path $encoded ($key+'.mp2')),'-f','s16le','-acodec','pcm_s16le','-ac',[string]$plan[$key],'-ar','48000',$pcm) 'Audio decoding'
        Assert-HpvrPcm $pcm $plan[$key]
        $manifest.Add([pscustomobject]@{path='Cache/Audio/'+$key;sha256=(Get-FileHash -LiteralPath $pcm -Algorithm SHA256).Hash;bytes=(Get-Item -LiteralPath $pcm).Length})
    }
    Write-Progress -Activity 'Preparing original music and speech' -Completed
    $prepareArguments=@($stage,'--output',$prepared,'--map','2')
    & $assertTool Prepare
    Invoke-HpvrChecked $tools.Prepare $prepareArguments 'Broomstick scene preparation' |
        Tee-Object -FilePath (Join-Path $privateRun 'prepared-scene-2.txt') | ForEach-Object { Write-Host $_ }
    & $assertTool Prepare
    Invoke-HpvrChecked $tools.Prepare ($prepareArguments+'--verify') 'Broomstick prepared-scene verification' |
        Tee-Object -FilePath (Join-Path $privateRun 'prepared-scene-2-verify.txt') | ForEach-Object { Write-Host $_ }
    $preparedFile=Join-Path $prepared 'map-2.hpvc'
    Assert-HpvrNoLinks $preparedFile
    $preparedHash=(Get-FileHash -LiteralPath $preparedFile -Algorithm SHA256).Hash
    $destination=Join-Path $cache 'map-2.hpvc'
    Copy-Item -LiteralPath $preparedFile -Destination $destination
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $preparedHash) { throw 'Prepared scene copy failed.' }
    $manifest.Add([pscustomobject]@{path='Cache/Scenes/map-2.hpvc';sha256=$preparedHash;bytes=(Get-Item -LiteralPath $destination).Length})
    foreach ($entry in $manifest) {
        Assert-HpvrSafeRelative $entry.path
        $file=Join-Path $stage $entry.path
        Assert-HpvrNoLinks $file
        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Prepared data changed: $($entry.path)" }
    }
    if (@(Get-ChildItem -LiteralPath $stage -File -Recurse -Force).Count -ne $manifest.Count) { throw 'Unexpected file in prepared data tree.' }
    foreach ($inputFile in $dependencies.Inputs) {
        $entry=@($manifest | Where-Object {$_.path -ceq $inputFile.Relative})[0]
        if ((Get-FileHash -LiteralPath $inputFile.Source -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Original input changed during preparation: $($inputFile.Relative)" }
    }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $privateRun 'PRIVATE-owned-data-manifest.json') -Encoding UTF8
    Write-Host "BROOM_PREPARE=PASS map=2 files=$($manifest.Count) audio_clips=$($plan.Count) private_path=$privateRun"
    Write-Host 'APK_INSTALL=NOT_PERFORMED DATA_PUSH=NOT_PERFORMED APP_LAUNCH=NOT_PERFORMED'
    Write-Host 'Do not publish the private HP tree, encoded audio, prepared caches or private manifest.'
}

if (-not $hpvrBroomLibraryOnly) { Invoke-HpvrBroomPreparation @hpvrBroomArguments }
