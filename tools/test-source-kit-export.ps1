[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = Split-Path -Parent $PSScriptRoot
$runRoot = Join-Path $repository ('local/source-kit-tests-' + [guid]::NewGuid().ToString('N'))
$fixture = Join-Path $runRoot 'source'
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$checks = 0

function Expect([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}
function Expect-Failure([scriptblock]$Action, [string]$Message) {
    $failed = $false
    try { & $Action } catch { $failed = $true }
    Expect $failed $Message
}
function Write-Fixture([string]$RelativePath, [string]$Contents = '// Synthetic source-kit boundary test') {
    $path = Join-Path $fixture $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
    [IO.File]::WriteAllText($path, $Contents, $utf8)
}

# These are deliberately synthetic text fixtures, not copied game data or code.
$required = @(
    '.gitattributes', '.gitignore', 'CMakeLists.txt', 'README.md',
    'INSTALL.bat', 'docs/BUILDING.md', 'tools/workspace/INSTALL-PLAYER.ps1',
    'tools/workspace/BUILD-QUEST-DEBUG.ps1', 'tools/workspace/PACKAGE-QUEST-DEBUG.ps1',
    'tools/workspace/BUILD-QUEST-RELEASE.ps1', 'tools/workspace/PACKAGE-QUEST-PLAYER.ps1',
    'tools/workspace/VERIFY-QUEST-APK.ps1', 'tools/workspace/IMPORT-QUEST-DATA.ps1', 'tools/workspace/PREPARE-QUEST-AUDIO.ps1',
    'tools/workspace/PREPARE-QUEST-FRONTEND.ps1', 'tools/workspace/RUN-LATEST-VR.cmd', 'android/build.gradle',
    'tools/workspace/PREPARE-QUEST-CHALLENGE.ps1', 'tools/workspace/PREPARE-QUEST-BROOM.ps1',
    'tools/test-quest-broom-preparation.ps1', 'tools/workspace/PREPARE-QUEST-CHARMS.ps1',
    'tools/test-quest-charms-preparation.ps1',
    'android/settings.gradle', 'android/gradle.properties', 'android/app/build.gradle',
    'tools/verify-baseline.ps1', 'tools/test-source-kit-export.ps1',
    'tools/release/INSTALL-HPVR.ps1', 'tools/release/INSTALL-HPVR.cmd',
    'tools/release/PLAYER-INSTALL.md', 'tools/release/TEST-PLAYER-INSTALL.ps1',
    'tools/release/TEST-ADB-BOOTSTRAP.ps1', 'tools/release/TEST-FFMPEG-BOOTSTRAP.ps1',
    'cmake/HPVRVoice.cmake', 'tools/voice/CMakeLists.txt',
    'tools/voice/FETCH-VOICE-DEPENDENCIES.ps1', 'tools/voice/BUILD-VOICE-CHECKS.ps1',
    'tools/voice/TEST-VOICE-APK-PAYLOAD.ps1', 'tools/voice/VOICE-ASSETS.psd1',
    'tools/voice/VOICE-APK-PAYLOAD.ps1',
    'tools/voice/BUILD-NEURAL-VOICE-RUNTIME.ps1', 'tools/voice/NEURAL-RUNTIME-NOTICES.psd1',
    'tools/voice/TEST-NEURAL-VOICE-RUNTIME.ps1',
    'tools/voice/flipendo.keywords', 'tools/voice/voice_keyword_probe.cpp', 'tools/voice/README.md',
    'tools/xr-runtime-probe/Cargo.toml', 'tools/xr-runtime-probe/Cargo.lock',
    'tools/xr-runtime-probe/rust-toolchain.toml', 'tools/xr-runtime-probe/build.rs',
    'src/CMakeLists.txt', 'src/test.cpp', 'src/header.h', 'src/fixture.c',
    'android/app/src/main/AndroidManifest.xml', 'android/app/src/main/cpp/test.cpp',
    'android/app/src/main/cpp/quest_challenge_runtime.inl',
    'tools/xr-runtime-probe/src/main.rs',
    'docs/architecture.md', 'docs/wand-gesture-contract.md',
    'docs/RELEASE-BUILD.md', 'docs/THIRD-PARTY-NOTICES.md', 'docs/VOICE-HOTFIX-0.1.2.1.md',
    'docs/CLASSIC-CASTING-AND-CHALLENGE.md', 'docs/FLIPENDO-CHALLENGE.md', 'docs/LOADING-AND-PICKUPS.md'
)
foreach ($relative in $required) { Write-Fixture $relative }
$exporter = Join-Path $fixture 'tools/workspace/EXPORT-SOURCE-KIT.ps1'
Copy-Item -LiteralPath (Join-Path $repository 'tools/workspace/EXPORT-SOURCE-KIT.ps1') -Destination $exporter
Write-Fixture 'local/never-export.cpp'
Write-Fixture 'src/build/never-export.cpp'
Write-Fixture 'src/assets/never-export.cpp'
Write-Fixture 'src/never-export.u'
Write-Fixture 'src/never-export.png'
Write-Fixture 'android/app/src/main/assets/never-export.u'
Write-Fixture 'android/app/build/never-export.cpp'
Write-Fixture 'local/signing/release/never-export.p12'
Write-Fixture 'local/signing/release/never-export.clixml'
Write-Fixture 'tools/release/never-export.ps1'
Write-Fixture 'tools/release/assets/never-export.md'
Write-Fixture 'local/voice-dependencies/assets/hpvr-voice/en-us/never-export-model'
Write-Fixture 'local/neural-voice-dependencies/assets/hpvr-voice/never-export-model.onnx'
Write-Fixture 'tools/voice/never-export-model.bin'
Write-Fixture 'tools/voice/assets/never-export-model.md'
$excludedDocs = @('AGENTS.md', 'docs/unknown-note.md', 'docs/runtime-evidence-fixture.md')
foreach ($relative in $excludedDocs) { Write-Fixture $relative }

$destination = Join-Path $runRoot 'kit'
& $exporter -DestinationPath $destination -AuditOnly
Expect (-not (Test-Path -LiteralPath $destination)) 'Audit-only created output.'
& $exporter -DestinationPath $destination -CreateArchive
Expect (Test-Path -LiteralPath (Join-Path $destination 'src/test.cpp')) 'Source did not export.'
Expect (Test-Path -LiteralPath (Join-Path $destination 'android/app/src/main/cpp/quest_challenge_runtime.inl')) 'Inline gameplay source missing.'
Expect (Test-Path -LiteralPath (Join-Path $destination 'SOURCE-SHA256.txt')) 'Manifest is missing.'
Expect (@(Get-ChildItem -LiteralPath $destination -File -Filter '*.ps1').Count -eq 0) 'PowerShell scripts must not clutter the root.'
Expect (-not (Test-Path -LiteralPath (Join-Path $destination 'AGENTS.md'))) 'Private repository instructions exported.'
Expect (Test-Path -LiteralPath (Join-Path $destination 'INSTALL.bat')) 'Player entry point missing.'
Expect (@(Get-ChildItem -LiteralPath $destination -Recurse -Filter 'never-export*').Count -eq 0) 'Excluded content exported.'
foreach ($relative in @('cmake/HPVRVoice.cmake', 'tools/voice/FETCH-VOICE-DEPENDENCIES.ps1',
        'tools/voice/VOICE-ASSETS.psd1', 'tools/voice/flipendo.keywords', 'tools/voice/README.md',
        'tools/voice/BUILD-NEURAL-VOICE-RUNTIME.ps1', 'tools/voice/NEURAL-RUNTIME-NOTICES.psd1',
        'tools/voice/TEST-NEURAL-VOICE-RUNTIME.ps1')) {
    Expect (Test-Path -LiteralPath (Join-Path $destination $relative)) "Voice build source missing: $relative"
}
foreach ($relative in $excludedDocs) {
    Expect (-not (Test-Path -LiteralPath (Join-Path $destination $relative))) "Non-public documentation exported: $relative"
}
foreach ($relative in @('docs/architecture.md', 'docs/wand-gesture-contract.md', 'docs/RELEASE-BUILD.md', 'docs/THIRD-PARTY-NOTICES.md', 'docs/VOICE-HOTFIX-0.1.2.1.md')) {
    Expect (Test-Path -LiteralPath (Join-Path $destination $relative)) "Maintained documentation missing: $relative"
}
$manifest = @(Get-Content -LiteralPath (Join-Path $destination 'SOURCE-SHA256.txt'))
foreach ($line in $manifest) {
    Expect ($line -match '^([0-9A-F]{64})  (.+)$') 'Malformed source hash record.'
    Expect ((Get-FileHash -LiteralPath (Join-Path $destination $Matches[2]) -Algorithm SHA256).Hash -eq $Matches[1]) 'Copy hash mismatch.'
}
Expect (Test-Path -LiteralPath ($destination + '.zip')) 'Source archive is missing.'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($destination + '.zip')
try {
    Expect ($archive.Entries.Count -eq $manifest.Count + 2) 'Archive inventory mismatch.'
    foreach ($entry in $archive.Entries) {
        Expect (-not $entry.FullName.Contains('\')) 'Archive uses nonportable backslash names.'
        $local = Join-Path $destination $entry.FullName
        Expect (Test-Path -LiteralPath $local -PathType Leaf) 'Archive contains an unexpected file.'
        $hasher = [Security.Cryptography.SHA256]::Create()
        $stream = $entry.Open()
        try { $hash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose(); $hasher.Dispose() }
        Expect ($hash -eq (Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash) 'Archive content hash mismatch.'
    }
} finally { $archive.Dispose() }
Expect-Failure { & $exporter -DestinationPath $destination } 'Existing output was overwritten.'
Expect-Failure { & $exporter -DestinationPath (Join-Path $fixture 'nested') } 'Nested output was allowed.'
Expect-Failure { & $exporter -DestinationPath $runRoot } 'Ancestor output was allowed.'
$archiveConflict = Join-Path $runRoot 'archive-conflict'
[IO.File]::WriteAllText($archiveConflict + '.zip', 'synthetic existing archive', $utf8)
Expect-Failure { & $exporter -DestinationPath $archiveConflict -CreateArchive } 'Existing archive was overwritten.'
Expect (-not (Test-Path -LiteralPath $archiveConflict)) 'Archive conflict created a kit.'

$blockedOutput = Join-Path $runRoot 'blocked'
$badSource = Join-Path $fixture 'src/test.cpp'
foreach ($signature in @(
        [byte[]]@(0xC1, 0x83, 0x2A, 0x9E), # Unreal package magic only; synthetic
        [byte[]]@(0x4D, 0x5A, 0x90, 0),
        [byte[]]@(0x50, 0x4B, 0x03, 0x04),
        [byte[]]@(0x7F, 0x45, 0x4C, 0x46),
        [byte[]]@(0x41, 0x00, 0x42, 0x43),
        [byte[]]@(0xFF, 0xFF, 0xFF, 0xFF))) {
    [IO.File]::WriteAllBytes($badSource, $signature)
    Expect-Failure { & $exporter -DestinationPath $blockedOutput } 'Binary data disguised as source was allowed.'
    Expect (-not (Test-Path -LiteralPath $blockedOutput)) 'Failed source audit finalized a kit.'
}
Write-Fixture 'src/test.cpp'

# Do not need developer mode/symlink rights: Windows directory junctions are
# sufficient to exercise the recursion boundary. Test output remains in local/.
if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) {
    $junction = Join-Path $fixture 'src/escaped'
    New-Item -ItemType Junction -Path $junction -Target $destination | Out-Null
    Expect-Failure { & $exporter -DestinationPath $blockedOutput -AuditOnly } 'Junction source was traversed.'
}
Write-Host "[hpvr.source-kit.tests] status=PASS checks=$checks artifacts=$runRoot"
