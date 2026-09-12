[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$manifestPath = Join-Path $PSScriptRoot 'VOICE-ASSETS.psd1'
$manifest = Import-PowerShellDataFile -LiteralPath $manifestPath
$assets = Join-Path $repo 'local/neural-voice-dependencies/assets/hpvr-voice'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$parseErrors = $null
$tokens = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'VOICE-APK-PAYLOAD.ps1'),
    [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw 'APK verifier has syntax errors.' }
$function = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Assert-HpvrApkPayload'
}, $true)
if ($null -eq $function) { throw 'APK payload verifier function is missing.' }
. ([scriptblock]::Create($function.Extent.Text))
$script:checks = 0

function Check-Payload([string]$Name, [bool]$VoiceBuild, [bool]$Expected,
                       [string]$Omit = '', [string]$Corrupt = '', [string]$Extra = '',
                       [string]$Duplicate = '', [bool]$IncludeModel = $true) {
    $memory = [IO.MemoryStream]::new()
    try {
        $zip = [IO.Compression.ZipArchive]::new($memory, [IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            if ($IncludeModel) {
                foreach ($relative in $manifest.Keys) {
                    if ($relative -ceq $Omit) { continue }
                    $bytes = [IO.File]::ReadAllBytes((Join-Path $assets $relative))
                    if ($relative -ceq $Corrupt) { $bytes[0] = $bytes[0] -bxor 1 }
                    $entry = $zip.CreateEntry("assets/hpvr-voice/$relative")
                    $output = $entry.Open()
                    try { $output.Write($bytes, 0, $bytes.Length) } finally { $output.Dispose() }
                }
            }
            foreach ($nameToAdd in @($Extra, $Duplicate)) {
                if ($nameToAdd.Length -eq 0) { continue }
                $entry = $zip.CreateEntry($nameToAdd)
                $output = $entry.Open()
                try { $output.WriteByte(42) } finally { $output.Dispose() }
            }
        } finally { $zip.Dispose() }
        $memory.Position = 0
        $zip = [IO.Compression.ZipArchive]::new($memory, [IO.Compression.ZipArchiveMode]::Read, $true)
        $accepted = $false
        try {
            try { Assert-HpvrApkPayload $zip $VoiceBuild $manifestPath; $accepted = $true }
            catch { if ($Expected) { throw "${Name}: $($_.Exception.Message)" } }
        } finally { $zip.Dispose() }
        if ($accepted -ne $Expected) { throw "Unexpected APK payload result: $Name" }
        $script:checks++
    } finally { $memory.Dispose() }
}

Check-Payload 'exact licensed model' $true $true
Check-Payload 'legacy no assets' $false $true -IncludeModel $false
Check-Payload 'legacy rejects model' $false $false
Check-Payload 'model required in C47' $true $false -IncludeModel $false
Check-Payload 'missing source license' $true $false -Omit 'LICENSE-sherpa-onnx.txt'
Check-Payload 'missing model license' $true $false -Omit 'MODEL-README.md'
Check-Payload 'missing transitive notices' $true $false -Omit 'NOTICE-onnxruntime-third-party.txt'
Check-Payload 'tampered model' $true $false -Corrupt 'encoder.int8.onnx'
Check-Payload 'tampered keywords' $true $false -Corrupt 'flipendo.keywords'
Check-Payload 'unlisted model file' $true $false -Extra 'assets/hpvr-voice/custom.bin'
Check-Payload 'renamed voice prefix' $true $false -Extra 'assets/HPVR-voice/encoder.int8.onnx'
Check-Payload 'duplicate ZIP path' $true $false -Duplicate 'assets/hpvr-voice/encoder.int8.onnx'
Check-Payload 'reject old acoustic assets' $true $false -Extra 'assets/hpvr-voice/en-us/means'
Check-Payload 'reject microphone recording' $true $false -Extra 'assets/hpvr-voice/microphone.raw'
foreach ($recording in @('res/raw/capture.pcm', 'res/raw/microphone.raw',
        'META-INF/capture.pcm', 'META-INF/MICROPHONE.RAW',
        'res/raw/voice.flac', 'res/raw/voice.m4a', 'res/raw/voice.aac',
        'res/raw/voice.opus', 'res/raw/voice.aif', 'res/raw/voice.aiff',
        'res/raw/voice.amr', 'res/raw/voice.caf', 'res/raw/voice.wma',
        'res/raw/recording.webm', 'res/raw/recording.mp4',
        'res/raw/recording.m4v', 'res/raw/recording.3gp')) {
    Check-Payload "reject recording anywhere: $recording" $true $false -Extra $recording
    Check-Payload "legacy rejects recording anywhere: $recording" $false $false -IncludeModel $false -Extra $recording
}
Check-Payload 'unrelated asset' $true $false -Extra 'assets/anything.bin'
Check-Payload 'game map' $true $false -Extra 'assets/Maps/Lev_Tut1.unr'
Check-Payload 'game package in resources' $true $false -Extra 'res/raw/game.utx'
Check-Payload 'game audio in resources' $true $false -Extra 'res/raw/dialogue.wav'
Check-Payload 'prepared scene' $true $false -Extra 'res/raw/map-0.hpvc'
Check-Payload 'private signing key' $true $false -Extra 'META-INF/private.p12'
Check-Payload 'path traversal' $true $false -Extra 'assets/hpvr-voice/../other'
Check-Payload 'backslash path' $true $false -Extra 'assets\hpvr-voice\other'
Check-Payload 'absolute path' $true $false -Extra '/assets/hpvr-voice/other'
foreach ($relative in $manifest.Keys) {
    Check-Payload "every pinned file is mandatory: $relative" $true $false -Omit $relative
}
Write-Host "Voice APK payload checks: $script:checks PASS. No APK, game files or device changed."
