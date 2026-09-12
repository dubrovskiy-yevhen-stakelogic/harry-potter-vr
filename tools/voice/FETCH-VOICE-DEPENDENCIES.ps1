[CmdletBinding()]
param([switch]$Offline)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = Join-Path $repo 'local/neural-voice-dependencies'
$assets = Join-Path $root 'assets/hpvr-voice'
$manifest = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot 'VOICE-ASSETS.psd1')
$noticeManifest = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot 'NEURAL-RUNTIME-NOTICES.psd1')
$modelName = 'sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01'
$hostName = 'sherpa-onnx-v1.13.7-win-x64-shared-MD-Release-no-tts'

function Assert-NoLinks([string]$Path) {
    $check = [IO.Path]::GetFullPath($Path)
    while ($check) {
        if ((Test-Path -LiteralPath $check) -and
            ((Get-Item -LiteralPath $check -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Refusing a reparse point: $check"
        }
        $parent = [IO.Path]::GetDirectoryName($check)
        if ($parent -eq $check) { break }
        $check = $parent
    }
}
function Assert-Hash([string]$Path, [string]$Expected) {
    Assert-NoLinks $Path
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -cne $Expected) {
        throw "Dependency integrity check failed: $Path"
    }
}
function Copy-IdenticalOrNew([string]$Source, [string]$Destination) {
    Assert-NoLinks $Source
    Assert-NoLinks $Destination
    $expected = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    if (Test-Path -LiteralPath $Destination) { Assert-Hash $Destination $expected; return }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
    [IO.File]::Copy($Source, $Destination, $false)
    Assert-Hash $Destination $expected
}
function Get-PinnedFile([string]$Url, [string]$Destination, [string]$Sha256) {
    Assert-NoLinks $Destination
    if (Test-Path -LiteralPath $Destination) { Assert-Hash $Destination $Sha256; return }
    if ($Offline) { throw "Pinned dependency is missing; run once without -Offline: $Destination" }
    if (-not $Url.StartsWith('https://', [StringComparison]::Ordinal)) { throw 'Only HTTPS dependency URLs are allowed.' }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
    $pending = "$Destination.download"
    Assert-NoLinks $pending
    if (Test-Path -LiteralPath $pending) { throw "A previous download exists; inspect it first: $pending" }
    Write-Host "Downloading pinned dependency: $Url"
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $pending
    Assert-Hash $pending $Sha256
    [IO.File]::Move($pending, $Destination)
}
function Expand-PinnedArchive([string]$Archive, [string]$Parent, [string]$Prefix) {
    $destination = Join-Path $Parent $Prefix
    Assert-NoLinks $destination
    if (Test-Path -LiteralPath $destination) { return $destination }
    $entries = @(& tar.exe -tjf $Archive)
    if ($LASTEXITCODE -ne 0 -or $entries.Count -lt 5) { throw "Cannot inspect archive: $Archive" }
    foreach ($entry in $entries) {
        if (-not $entry.StartsWith("$Prefix/", [StringComparison]::Ordinal) -or
            $entry -match '(^|/)\.\.(/|$)|[\\:\r\n]') { throw "Unsafe archive entry: $entry" }
    }
    $details = @(& tar.exe -tvjf $Archive)
    if ($LASTEXITCODE -ne 0 -or $details.Count -ne $entries.Count) { throw 'Cannot inspect archive file types.' }
    foreach ($entry in $details) {
        if ($entry.Length -eq 0 -or ($entry[0] -ne '-' -and $entry[0] -ne 'd')) {
            throw "Archive links and special files are not allowed: $entry"
        }
    }
    [IO.Directory]::CreateDirectory($Parent) | Out-Null
    $staging = Join-Path $Parent ('.extract-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($staging) | Out-Null
    & tar.exe -xjf $Archive -C $staging
    if ($LASTEXITCODE -ne 0) { throw "Extraction failed; temporary files retained at $staging" }
    $extracted = Join-Path $staging $Prefix
    Assert-NoLinks $extracted
    foreach ($item in Get-ChildItem -LiteralPath $extracted -Recurse -Force) { Assert-NoLinks $item.FullName }
    [IO.Directory]::Move($extracted, $destination)
    [IO.Directory]::Delete($staging, $false)
    return $destination
}

Assert-NoLinks $root
if ($manifest.Count -ne 24 -or $noticeManifest.Files.Count -ne 17) { throw 'Unexpected neural voice manifest shape.' }
$modelArchive = Join-Path $root "downloads/$modelName.tar.bz2"
$hostArchive = Join-Path $root "downloads/$hostName.tar.bz2"
Get-PinnedFile "https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/$modelName.tar.bz2" $modelArchive 'F170013B4716E41B62B9BFD809687C207CEF798EF9BC6534D524E17AF9B6561A'
Get-PinnedFile "https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.7/$hostName.tar.bz2" $hostArchive '269D078C31CB176CB7C2C87952E9A8B30B19541DF95445AAAA961C91A0760159'
$model = Expand-PinnedArchive $modelArchive (Join-Path $root 'models') $modelName
$hostRuntime = Expand-PinnedArchive $hostArchive (Join-Path $root 'host') $hostName
$hostFiles = @{
    'include/sherpa-onnx/c-api/c-api.h' = '55E97B3D579316F951164801817BC1B1A8C64A98DACA5E2C45B6EA4ABAE32BEC'
    'lib/sherpa-onnx-c-api.lib' = 'AA04E30CD0D9386FA2446B5B8CC8DFF6DCF1C15D1263BE1BCFC86CF92F6663D2'
    'lib/sherpa-onnx-c-api.dll' = '24342A270914E44E8E50036139EF96064F19B74724244FB10B1B9B45978F31A2'
    'lib/onnxruntime.dll' = '4EE0AE76CBF51BDE6999F36829939B2B06D340AB57867EF82B61A0B674111EFA'
    'lib/onnxruntime_providers_shared.dll' = 'CD7245821AD7054D1904AC221CA3D6B913C0E8977F0B408787F3BC2C430A5403'
    'lib/onnxruntime.lib' = 'B9FC3CD678257D88A111B0773EDE4BFCEAF0FE95DAAB4379F2B2B37348A68781'
}
foreach ($entry in $hostFiles.GetEnumerator()) { Assert-Hash (Join-Path $hostRuntime $entry.Key) $entry.Value }

$modelFiles = @{
    'encoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx' = 'encoder.int8.onnx'
    'decoder-epoch-12-avg-2-chunk-16-left-64.onnx' = 'decoder.onnx'
    'joiner-epoch-12-avg-2-chunk-16-left-64.int8.onnx' = 'joiner.int8.onnx'
    'tokens.txt' = 'tokens.txt'
    'README.md' = 'MODEL-README.md'
}
foreach ($entry in $modelFiles.GetEnumerator()) {
    $source = Join-Path $model $entry.Key
    Assert-Hash $source $manifest[$entry.Value]
    Copy-IdenticalOrNew $source (Join-Path $assets $entry.Value)
}
foreach ($notice in $noticeManifest.Files) {
    if ($notice.Name -notmatch '^[A-Za-z0-9_.-]+\.txt$' -or $manifest[$notice.Name] -cne $notice.Sha256) {
        throw "Invalid notice manifest entry: $($notice.Name)"
    }
    $source = Join-Path $root "licenses/$($notice.Name)"
    Get-PinnedFile $notice.Url $source $notice.Sha256
    if ((Get-Item -LiteralPath $source).Length -ne $notice.Bytes) { throw "Unexpected notice length: $source" }
    Copy-IdenticalOrNew $source (Join-Path $assets $notice.Name)
}
# The model archive README explicitly grants Apache-2.0; keep both that
# attribution/provenance and the complete license text beside the weights.
Copy-IdenticalOrNew (Join-Path $root 'licenses/LICENSE-sherpa-onnx.txt') (Join-Path $assets 'LICENSE-model.txt')
$keywords = Join-Path $PSScriptRoot 'flipendo.keywords'
Assert-Hash $keywords $manifest['flipendo.keywords']
$keywordTarget = Join-Path $assets 'flipendo.keywords'
Assert-NoLinks $keywordTarget
if (Test-Path -LiteralPath $keywordTarget) {
    $previousKeywordHash = (Get-FileHash -LiteralPath $keywordTarget -Algorithm SHA256).Hash
    if ($previousKeywordHash -cne $manifest['flipendo.keywords']) {
        # Only our known previous vocabulary may migrate. Third-party weights,
        # user edits and unknown keyword files retain the strict no-replace rule.
        if (@('AF2C96A1130127BD9A7A82002BFC307D5E9A555D980AF58D96CEB0927495564C',
              '2A832EEA7B6F8791E5B510E9D3DC982A29840640157C6D3C4141CBFBCE6B91D2',
              'B772267DFCED642A7C3010EA368487BD43C4A3CBFD394DC543D6A66DE3FF00AF') -cnotcontains $previousKeywordHash) {
            throw 'Unknown staged keyword file; preserve and inspect it before updating.'
        }
        $keywordBackup = Join-Path $root ('keyword-history/' + $previousKeywordHash + '.keywords')
        Copy-IdenticalOrNew $keywordTarget $keywordBackup
        $pendingKeywords = $keywordTarget + '.pending'
        Assert-NoLinks $pendingKeywords
        if (Test-Path -LiteralPath $pendingKeywords) {
            Assert-Hash $pendingKeywords $manifest['flipendo.keywords']
        } else {
            [IO.File]::Copy($keywords, $pendingKeywords, $false)
        }
        Assert-Hash $pendingKeywords $manifest['flipendo.keywords']
        Assert-Hash $keywordTarget $previousKeywordHash
        [IO.File]::Replace($pendingKeywords, $keywordTarget, [NullString]::Value)
    }
}
Copy-IdenticalOrNew $keywords $keywordTarget
foreach ($entry in $manifest.GetEnumerator()) { Assert-Hash (Join-Path $assets $entry.Key) $entry.Value }
foreach ($file in Get-ChildItem -LiteralPath $assets -Recurse -Force) {
    Assert-NoLinks $file.FullName
    if ($file.PSIsContainer) { throw "Unexpected voice asset directory: $($file.FullName)" }
    if (@($manifest.Keys) -cnotcontains $file.Name) { throw "Unexpected voice asset: $($file.Name)" }
}
Write-Host "Voice host runtime: $hostRuntime"
Write-Host "Voice model assets: $assets"
Write-Host '24 pinned model/notice files are ready. No game files, user recordings, microphone, APK or device were touched.'
Write-Host 'Android native libraries are prepared separately by BUILD-NEURAL-VOICE-RUNTIME.ps1.'
