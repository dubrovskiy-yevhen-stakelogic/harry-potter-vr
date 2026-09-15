[CmdletBinding()]
param(
    [string]$NativeLibraryPath,
    [string]$ApkPath
)

$ErrorActionPreference = 'Stop'
if ($PSBoundParameters.Count -ne 0) {
    throw 'Native-library splicing is no longer supported. Run BUILD-QUEST-DEBUG.ps1 for a complete matching APK, or BUILD-QUEST-RELEASE.ps1 for distribution. No APK was modified.'
}

# Gradle packages Java, native code, resources and the voice model together.
& (Join-Path $PSScriptRoot 'BUILD-QUEST-DEBUG.ps1')
& (Join-Path $PSScriptRoot 'VERIFY-QUEST-APK.ps1')
