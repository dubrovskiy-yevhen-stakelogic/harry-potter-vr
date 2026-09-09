[CmdletBinding()]
param(
    [string]$NativeLibraryPath = "build\quest-c3-android\libhpvr_quest.so",
    [string]$ApkPath = "android\app\build\outputs\apk\debug\app-debug.apk"
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$NativeLibraryPath = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot $NativeLibraryPath))
$ApkPath = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot $ApkPath))
$buildRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'build'))

foreach ($required in @($NativeLibraryPath, $ApkPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required package input is missing: $required"
    }
}
if (-not $NativeLibraryPath.StartsWith(
        [System.IO.Path]::GetFullPath($repositoryRoot),
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Native library must come from this repository'
}

$buildTools = 'C:\Dev\android-toolchain\sdk\build-tools\35.0.0'
$zipalign = Join-Path $buildTools 'zipalign.exe'
$apksigner = Join-Path $buildTools 'apksigner.bat'
$debugKeystore = Join-Path $env:USERPROFILE '.android\debug.keystore'
foreach ($required in @($zipalign, $apksigner, $debugKeystore)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required packaging tool is missing: $required"
    }
}

$temporaryDirectory = Join-Path $buildRoot (
    'quest-apk-package-' + [guid]::NewGuid().ToString('N'))
$temporaryDirectory = [System.IO.Path]::GetFullPath($temporaryDirectory)
if (-not $temporaryDirectory.StartsWith(
        $buildRoot + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to create a package directory outside the build root'
}
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
$sourceApk = Join-Path $temporaryDirectory 'container.apk'
$unsignedApk = Join-Path $temporaryDirectory 'unsigned.apk'
$alignedApk = Join-Path $temporaryDirectory 'aligned.apk'
Copy-Item -LiteralPath $ApkPath -Destination $sourceApk

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$source = [System.IO.Compression.ZipFile]::OpenRead($sourceApk)
$destination = [System.IO.Compression.ZipFile]::Open(
    $unsignedApk, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($entry in $source.Entries) {
        if ($entry.FullName -eq 'lib/arm64-v8a/libhpvr_quest.so' -or
            $entry.FullName -match '^META-INF/.*\.(RSA|SF|MF)$') {
            continue
        }
        $store = $entry.FullName -eq 'resources.arsc' -or
                 $entry.FullName -like 'lib/*'
        $level = if ($store) {
            [System.IO.Compression.CompressionLevel]::NoCompression
        } else {
            [System.IO.Compression.CompressionLevel]::Optimal
        }
        $copy = $destination.CreateEntry($entry.FullName, $level)
        $copy.LastWriteTime = $entry.LastWriteTime
        if (-not $entry.FullName.EndsWith('/')) {
            $inputStream = $entry.Open()
            $outputStream = $copy.Open()
            try {
                $inputStream.CopyTo($outputStream)
            } finally {
                $outputStream.Dispose()
                $inputStream.Dispose()
            }
        }
    }
    $nativeEntry = $destination.CreateEntry(
        'lib/arm64-v8a/libhpvr_quest.so',
        [System.IO.Compression.CompressionLevel]::NoCompression)
    $nativeInput = [System.IO.File]::OpenRead($NativeLibraryPath)
    $nativeOutput = $nativeEntry.Open()
    try {
        $nativeInput.CopyTo($nativeOutput)
    } finally {
        $nativeOutput.Dispose()
        $nativeInput.Dispose()
    }
} finally {
    $destination.Dispose()
    $source.Dispose()
}

try {
    & $zipalign -f -P 16 4 $unsignedApk $alignedApk
    if ($LASTEXITCODE -ne 0) {
        throw "zipalign failed with exit code $LASTEXITCODE"
    }
    & $apksigner sign --ks $debugKeystore --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out $ApkPath $alignedApk
    if ($LASTEXITCODE -ne 0) {
        throw "apksigner failed with exit code $LASTEXITCODE"
    }
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $ApkPath
    Write-Host "[hpvr.quest.apk.package] status=PASS"
    Write-Host "[hpvr.quest.apk.package] path=$ApkPath"
    Write-Host "[hpvr.quest.apk.package] sha256=$($hash.Hash)"
} finally {
    if ($temporaryDirectory.StartsWith(
            $buildRoot + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
