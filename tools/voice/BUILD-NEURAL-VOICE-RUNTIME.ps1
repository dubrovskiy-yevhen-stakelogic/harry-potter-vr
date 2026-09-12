[CmdletBinding()]
param(
    [string]$NdkPath = '',
    [string]$CMakePath = '',
    [string]$NinjaPath = '',
    [ValidateRange(1,32)][int]$Jobs = 4,
    [switch]$Offline,
    [switch]$VerifyOnly,
    [switch]$BuildOnly,
    [switch]$ReplaceKnownRuntime
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$root = Join-Path $repo 'local/neural-voice-dependencies'
$native = Join-Path $root 'native'
$output = Join-Path $root 'android-arm64'
$commit = '917bed95c8e5c7c18aa4d69fea42e9ef8ef0a60e'
$sourceHash = 'ACF539E930283442C4237B7B23A06EBE3BFF10CBC00694A4F09A3580E3C10E9E'
$ortHash = 'B36CEC051D8CAD6EC7ED3120B03ADC9AAD006E3D4B985AF5AC0EEB99504E2D99'
$patchBefore = '98BB45ADF41E78CC87A4718BF34B3BD96554472BF1DAFABBC49431662E4E4403'
$patchAfter = 'C27AD8463D43852893F110BD2BDD86C0498CAA4DFA784CE87B2CA29EEED8D058'
$utf8 = [Text.UTF8Encoding]::new($false)

function Assert-SafePath([string]$Path) {
    $absolute = [IO.Path]::GetFullPath($Path)
    if (-not $absolute.StartsWith(($root + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase) -and $absolute -ne $root) {
        throw "Native voice output must remain inside $root"
    }
    $check = $absolute
    while ($check) {
        if (Test-Path -LiteralPath $check) {
            if ((Get-Item -LiteralPath $check -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse points are not allowed: $check"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($check)
        if ($parent -eq $check) { break }
        $check = $parent
    }
    return $absolute
}
function Assert-Hash([string]$Path,[string]$Expected) {
    [void](Assert-SafePath $Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Expected) {
        throw "Unexpected or edited file: $Path"
    }
}
function Assert-Runtime([string]$Directory) {
    $manifestPath = Join-Path $Directory 'runtime-manifest.json'
    [void](Assert-SafePath $manifestPath)
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Runtime has no ownership manifest: $Directory"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or $manifest.sherpaCommit -ne $commit -or $manifest.sourceSha256 -ne $sourceHash -or
        $manifest.ortSha256 -ne $ortHash -or $manifest.patchSha256 -ne $patchAfter -or
        $manifest.ortVersion -ne '1.27.1' -or $manifest.ndk -ne '27.2.12479018' -or
        $manifest.abi -ne 'arm64-v8a' -or $manifest.androidApi -ne 32 -or $manifest.stl -ne 'c++_static' -or
        $manifest.tts -ne $false -or $manifest.jni -ne $false) {
        throw 'Runtime manifest does not describe the pinned CPU-only build.'
    }
    $seen = @{}
    foreach ($file in $manifest.files) {
        if ($file.path -notmatch '^(lib/lib[A-Za-z0-9_-]+\.a|include/sherpa-onnx/c-api/c-api\.h)$' -or $seen.ContainsKey($file.path)) {
            throw 'Unsafe or duplicate runtime manifest entry.'
        }
        $seen[$file.path] = $true
        Assert-Hash (Join-Path $Directory $file.path) $file.sha256
    }
    foreach ($name in $libraries) { if (-not $seen.ContainsKey("lib/$name")) { throw "Runtime manifest omits $name" } }
    if ($seen.Count -ne 11 -or -not $seen.ContainsKey('include/sherpa-onnx/c-api/c-api.h')) { throw 'Incomplete runtime manifest.' }
    foreach ($item in Get-ChildItem -LiteralPath $Directory -Recurse -Force) {
        [void](Assert-SafePath $item.FullName)
        if (-not $item.PSIsContainer) {
            $relative = $item.FullName.Substring(([IO.Path]::GetFullPath($Directory)).Length + 1).Replace('\','/')
            if ($relative -ne 'runtime-manifest.json' -and -not $seen.ContainsKey($relative)) {
                throw "Unknown native runtime file: $relative"
            }
        }
    }
}
$libraries = @('libsherpa-onnx-c-api.a','libsherpa-onnx-core.a','libkaldi-decoder-core.a',
    'libsherpa-onnx-kaldifst-core.a','libsherpa-onnx-fstfar.a','libsherpa-onnx-fst.a',
    'libkaldi-native-fbank-core.a','libkissfft-float.a','libonnxruntime.a','libssentencepiece_core.a')
if (($BuildOnly -and $ReplaceKnownRuntime) -or ($VerifyOnly -and ($BuildOnly -or $ReplaceKnownRuntime))) {
    throw 'Choose only one of -VerifyOnly, -BuildOnly, and -ReplaceKnownRuntime.'
}
if ($VerifyOnly) {
    Assert-Runtime $output
    Write-Host 'Pinned CPU-only ARM64 neural voice runtime: VERIFIED (no build or microphone use).'
    return
}
if (Test-Path -LiteralPath $output) {
    Assert-Runtime $output
    if (-not $ReplaceKnownRuntime -and -not $BuildOnly) {
        Write-Host 'Verified existing CPU-only runtime. Use -ReplaceKnownRuntime to rebuild it.'
        return
    }
}
if (-not $NdkPath) {
    foreach ($candidate in @($env:ANDROID_NDK_HOME,$env:ANDROID_NDK_ROOT,$env:ANDROID_NDK,
        (Join-Path $env:LOCALAPPDATA 'Android/Sdk/ndk/27.2.12479018'),'C:/Dev/android-toolchain/sdk/ndk/27.2.12479018')) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'source.properties'))) { $NdkPath=$candidate; break }
    }
}
if (-not $NdkPath -or -not (Test-Path -LiteralPath (Join-Path $NdkPath 'source.properties'))) { throw 'Provide -NdkPath for Android NDK 27.2.12479018.' }
if ((Get-Content -LiteralPath (Join-Path $NdkPath 'source.properties') -Raw) -notmatch 'Pkg.Revision\s*=\s*27\.2\.12479018') { throw 'This pinned build requires Android NDK 27.2.12479018.' }
if (-not $CMakePath) {
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmd) { $CMakePath=$cmd.Source }
    elseif (Test-Path -LiteralPath 'C:/Dev/android-toolchain/sdk/cmake/3.22.1/bin/cmake.exe') { $CMakePath='C:/Dev/android-toolchain/sdk/cmake/3.22.1/bin/cmake.exe' }
}
if (-not $CMakePath) { throw 'Provide -CMakePath.' }
if (-not $NinjaPath) {
    $candidate = Join-Path ([IO.Path]::GetDirectoryName($CMakePath)) 'ninja.exe'
    if (Test-Path -LiteralPath $candidate) { $NinjaPath=$candidate }
    else { $cmd=Get-Command ninja.exe -ErrorAction SilentlyContinue; if($cmd){$NinjaPath=$cmd.Source} }
}
if (-not $NinjaPath) { throw 'Provide -NinjaPath.' }
$strip = Join-Path $NdkPath 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'
[void](Assert-SafePath $native)
[void](New-Item -ItemType Directory -Path $native -Force)
$downloads=Join-Path $native 'downloads'
[void](New-Item -ItemType Directory -Path $downloads -Force)
function Get-Pinned([string]$Name,[string]$Url,[string]$Hash) {
    $path = Join-Path $downloads $Name
    [void](Assert-SafePath $path)
    if (-not (Test-Path -LiteralPath $path)) {
        if ($Offline) { throw "Offline dependency missing: $Name" }
        if (([uri]$Url).Scheme -ne 'https') { throw 'Only HTTPS dependency downloads are allowed.' }
        $partial = "$path.download"
        if (Test-Path -LiteralPath $partial) { throw "Unfinished download exists: $partial" }
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $partial
        Assert-Hash $partial $Hash
        Move-Item -LiteralPath $partial -Destination $path
    }
    Assert-Hash $path $Hash
    return $path
}
function Expand-Pinned([string]$Archive,[string]$Destination,[string[]]$Members=@()) {
    [void](Assert-SafePath $Destination)
    if (Test-Path -LiteralPath $Destination) { throw "Refusing to overwrite source extraction: $Destination" }
    $entries = @(& tar.exe -tf $Archive)
    if ($LASTEXITCODE -ne 0 -or $entries.Count -eq 0) { throw "Invalid archive: $Archive" }
    foreach ($entry in $entries) {
        if ($entry -match '(^[/\\]|^[A-Za-z]:|(^|[/\\])\.\.([/\\]|$))') { throw "Unsafe archive entry: $entry" }
    }
    [void](New-Item -ItemType Directory -Path $Destination)
    & tar.exe -xf $Archive --strip-components 1 -C $Destination @Members
    if ($LASTEXITCODE -ne 0) { throw "Could not extract dependency: $Archive" }
    if (Get-ChildItem -LiteralPath $Destination -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1) {
        throw 'Dependency extraction contains a reparse point.'
    }
}
$sourceArchive=Get-Pinned 'sherpa-source.tar.gz' "https://github.com/k2-fsa/sherpa-onnx/archive/$commit.tar.gz" $sourceHash
$ortArchive=Get-Pinned 'onnxruntime-static.zip' 'https://github.com/csukuangfj/onnxruntime-libs/releases/download/v1.27.1/onnxruntime-android-arm64-v8a-static_lib-1.27.1.zip' $ortHash
# All dependency URLs and SHA256 values are the pinned upstream CMake inputs.
$dependencies=@(
    @('kaldi_native_fbank','v1.22.3.tar.gz','https://github.com/csukuangfj/kaldi-native-fbank/archive/refs/tags/v1.22.3.tar.gz','9176cc66fc7ce1edf85cf355b06e320c57db6297df74277f575183468893cf61'),
    @('kissfft','kissfft.zip','https://github.com/mborgerding/kissfft/archive/febd4caeed32e33ad8b2e0bb5ea77542c40f18ec.zip','497103e664168ebe39580b757adbe616f6cf85a16572af581ca7bc42d0ab13fd'),
    @('kaldi_decoder','v0.3.0.tar.gz','https://github.com/k2-fsa/kaldi-decoder/archive/refs/tags/v0.3.0.tar.gz','b9f34cfb4fd3b1344100eead79ef4d37aa15962274b9e3056de345021f76a1b0'),
    @('kaldifst','v1.8.0.tar.gz','https://github.com/k2-fsa/kaldifst/archive/refs/tags/v1.8.0.tar.gz','3f247b7e5a2409071202f5e2bc6200060f66728c0a3443c03923ad2723e040b3'),
    @('openfst','v1.8.5-2026-07-09.tar.gz','https://github.com/csukuangfj/openfst/archive/refs/tags/v1.8.5-2026-07-09.tar.gz','2ff712a32952fcb01d351121a6bc8ccf4fdc6b2aa06ce8df2b3095dedd518c0e'),
    @('eigen','eigen-5.0.1.tar.gz','https://gitlab.com/libeigen/eigen/-/archive/5.0.1/eigen-5.0.1.tar.gz','e9c326dc8c05cd1e044c71f30f1b2e34a6161a3b6ecf445d56b53ff1669e3dec'),
    @('simple-sentencepiece','v0.7.tar.gz','https://github.com/pkufool/simple-sentencepiece/archive/refs/tags/v0.7.tar.gz','1748a822060a35baa9f6609f84efc8eb54dc0e74b9ece3d82367b7119fdc75af'),
    @('json','v3.12.0.tar.gz','https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz','4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187')
)
# Each build uses a private, fresh source tree; existing source edits are never overwritten.
$run=Join-Path $native ('b-'+[Guid]::NewGuid().ToString('N').Substring(0,12))
[void](New-Item -ItemType Directory -Path $run)
$source=Join-Path $run 'src'
Expand-Pinned $sourceArchive $source @("sherpa-onnx-$commit/CMakeLists.txt","sherpa-onnx-$commit/LICENSE","sherpa-onnx-$commit/cmake","sherpa-onnx-$commit/sherpa-onnx")
$ort=Join-Path $run 'ort'
Expand-Pinned $ortArchive $ort
# ORT 1.27 removed NNAPI headers; HPVR supports CPU only. This explicit,
# checksum-guarded dependency patch does not change CPU inference.
$session=Join-Path $source 'sherpa-onnx/csrc/session.cc'
Assert-Hash $session $patchBefore
$text=[IO.File]::ReadAllText($session)
if ([regex]::Matches($text,'(?m)^#if __ANDROID_API__ >= 27\r?$').Count -ne 2) { throw 'Unexpected NNAPI guard layout.' }
$text=$text.Replace('#if __ANDROID_API__ >= 27','#if __ANDROID_API__ >= 27 && defined(HPVR_SHERPA_ENABLE_NNAPI)')
[IO.File]::WriteAllText($session,$text,$utf8)
Assert-Hash $session $patchAfter
$overrides=@()
foreach($dep in $dependencies) {
    $archive=Get-Pinned $dep[1] $dep[2] $dep[3]
    $dest=Join-Path $run $dep[0]
    Expand-Pinned $archive $dest
    $overrides += "-DFETCHCONTENT_SOURCE_DIR_$($dep[0].ToUpperInvariant())=$dest"
}
$build=Join-Path $run 'build'
$oldLib=$env:SHERPA_ONNXRUNTIME_LIB_DIR
$oldInclude=$env:SHERPA_ONNXRUNTIME_INCLUDE_DIR
try {
    $env:SHERPA_ONNXRUNTIME_LIB_DIR=Join-Path $ort 'lib'
    $env:SHERPA_ONNXRUNTIME_INCLUDE_DIR=Join-Path $ort 'include'
    & $CMakePath -S $source -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$NinjaPath" "-DCMAKE_TOOLCHAIN_FILE=$NdkPath/build/cmake/android.toolchain.cmake" `
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-32 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF -DSHERPA_ONNX_ENABLE_TTS=OFF `
        -DSHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION=OFF -DSHERPA_ONNX_ENABLE_PORTAUDIO=OFF -DSHERPA_ONNX_ENABLE_WEBSOCKET=OFF `
        -DSHERPA_ONNX_ENABLE_PYTHON=OFF -DSHERPA_ONNX_ENABLE_JNI=OFF -DSHERPA_ONNX_ENABLE_C_API=ON `
        -DSHERPA_ONNX_ENABLE_BINARY=OFF -DSHERPA_ONNX_BUILD_C_API_EXAMPLES=OFF -DSHERPA_ONNX_ENABLE_TESTS=OFF `
        -DSHERPA_ONNX_ENABLE_GPU=OFF -DSHERPA_ONNX_ENABLE_QNN=OFF -DSHERPA_ONNX_ENABLE_RKNN=OFF `
        -DSHERPA_ONNX_USE_PRE_INSTALLED_ONNXRUNTIME_IF_AVAILABLE=ON -DCMAKE_DISABLE_FIND_PACKAGE_Git=ON `
        -DFETCHCONTENT_FULLY_DISCONNECTED=ON '-DCMAKE_CXX_FLAGS=-ffunction-sections -fdata-sections' '-DCMAKE_C_FLAGS=-ffunction-sections -fdata-sections' @overrides
    if($LASTEXITCODE -ne 0){throw 'CPU-only sherpa configure failed.'}
    & $CMakePath --build $build --target sherpa-onnx-c-api --parallel $Jobs
    if($LASTEXITCODE -ne 0){throw 'CPU-only sherpa build failed.'}
} finally {
    $env:SHERPA_ONNXRUNTIME_LIB_DIR=$oldLib
    $env:SHERPA_ONNXRUNTIME_INCLUDE_DIR=$oldInclude
}
$stage=Join-Path $run 'stage'
[void](New-Item -ItemType Directory -Path "$stage/lib","$stage/include/sherpa-onnx/c-api" -Force)
Copy-Item -LiteralPath "$source/sherpa-onnx/c-api/c-api.h" -Destination "$stage/include/sherpa-onnx/c-api/c-api.h"
foreach($name in $libraries) {
    $inputPath=Join-Path "$build/lib" $name
    if($name -eq 'libonnxruntime.a'){$inputPath=Join-Path "$ort/lib" $name}
    & $strip --strip-debug -o "$stage/lib/$name" $inputPath
    if($LASTEXITCODE -ne 0){throw "Could not remove debug sections from $name"}
}
$files=@(Get-ChildItem -LiteralPath $stage -File -Recurse | ForEach-Object {
    @{path=$_.FullName.Substring($stage.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$manifest=[ordered]@{schema=1;sherpaCommit=$commit;sourceSha256=$sourceHash;ortVersion='1.27.1';ortSha256=$ortHash;patchSha256=$patchAfter;abi='arm64-v8a';androidApi=32;ndk='27.2.12479018';stl='c++_static';tts=$false;jni=$false;files=$files}
[IO.File]::WriteAllText("$stage/runtime-manifest.json",($manifest|ConvertTo-Json -Depth 5),$utf8)
Assert-Runtime $stage
if ($BuildOnly) {
    Write-Host "Isolated CPU-only ARM64 runtime verified: $stage"
    Write-Host 'Current runtime was not changed; no application or microphone was started.'
    return
}
if(Test-Path -LiteralPath $output) {
    Assert-Runtime $output
    # Preserve the complete verified previous runtime; no unknown files are overwritten.
    $backup=Join-Path $native ('previous-'+[Guid]::NewGuid().ToString('N'))
    [void](Assert-SafePath $backup)
    Move-Item -LiteralPath $output -Destination $backup
    Write-Host "Previous native runtime preserved: $backup"
}
Move-Item -LiteralPath $stage -Destination $output
Write-Host "CPU-only ARM64 runtime prepared: $output"
Write-Host "Dependency patch: two NNAPI guards disabled; source SHA256 before=$patchBefore after=$patchAfter"
Write-Host 'No application was installed, launched, or given microphone access.'
