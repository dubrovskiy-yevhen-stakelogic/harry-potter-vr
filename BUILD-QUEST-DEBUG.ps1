[CmdletBinding()]
param([switch]$VoiceDiagnostics)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$androidProject = Join-Path $repositoryRoot 'android'
$sdkRoot = 'C:\Dev\android-toolchain\sdk'
$jdkRoot = 'C:\Dev\android-toolchain\jdk21'
$gradleRoot = Join-Path $env:USERPROFILE '.gradle\wrapper\dists'
$requiredGradleVersion = '9.3.1'

if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot 'ndk\27.2.12479018'))) {
    throw "Required Android NDK 27.2.12479018 is missing under $sdkRoot"
}
if (-not (Test-Path -LiteralPath (Join-Path $jdkRoot 'bin\java.exe'))) {
    throw "Required JDK is missing under $jdkRoot"
}

$gradle = Get-ChildItem -LiteralPath $gradleRoot -Recurse -Filter gradle.bat |
    Where-Object { $_.FullName -like "*\gradle-$requiredGradleVersion\bin\gradle.bat" } |
    Select-Object -First 1
if ($null -eq $gradle) {
    throw "Cached Gradle $requiredGradleVersion was not found under $gradleRoot"
}

# APK builds remain offline. Prepare licensed native dependencies explicitly;
# this preflight only checks their source-build identity and file hashes.
& (Join-Path $repositoryRoot 'tools/voice/BUILD-NEURAL-VOICE-RUNTIME.ps1') -VerifyOnly

$previousJavaHome = $env:JAVA_HOME
$previousAndroidHome = $env:ANDROID_HOME
$previousAndroidSdkRoot = $env:ANDROID_SDK_ROOT
try {
    $env:JAVA_HOME = $jdkRoot
    $env:ANDROID_HOME = $sdkRoot
    $env:ANDROID_SDK_ROOT = $sdkRoot
    Write-Host "Using Gradle: $($gradle.FullName)"
    $diagnosticProperty = '-PhpvrVoiceDiagnostics=' + ([bool]$VoiceDiagnostics).ToString().ToLowerInvariant()
    $buildTask = if ($VoiceDiagnostics) { ':app:assembleVoiceDiagnostic' } else { ':app:assembleDebug' }
    & $gradle.FullName --offline --no-daemon -p $androidProject $diagnosticProperty $buildTask
    if ($LASTEXITCODE -ne 0) {
        throw "Quest debug APK build failed with exit code $LASTEXITCODE"
    }
} finally {
    $env:JAVA_HOME = $previousJavaHome
    $env:ANDROID_HOME = $previousAndroidHome
    $env:ANDROID_SDK_ROOT = $previousAndroidSdkRoot
}

$apkRelative = if ($VoiceDiagnostics) { 'app\build\outputs\apk\voiceDiagnostic\app-voiceDiagnostic.apk' }
    else { 'app\build\outputs\apk\debug\app-debug.apk' }
$apk = Join-Path $androidProject $apkRelative
if (-not (Test-Path -LiteralPath $apk)) {
    throw "Gradle completed but the expected APK is missing: $apk"
}

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $apk
Write-Host "Quest APK (local voice diagnostic=$([bool]$VoiceDiagnostics)): $apk"
Write-Host "SHA256: $($hash.Hash)"
