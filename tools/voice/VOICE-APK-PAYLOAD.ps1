# Shared offline APK payload policy. Dot-source only; no download or device work.
function Assert-HpvrApkPayload($Archive, [bool]$VoiceBuild, [string]$VoiceManifest) {
    $voiceAssets = if ($VoiceBuild) { Import-PowerShellDataFile -LiteralPath $VoiceManifest } else { @{} }
    if ($VoiceBuild -and $voiceAssets.Count -ne 24) { throw 'Voice allowlist must contain exactly 24 licensed neural model/notice files.' }
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
    foreach ($entry in $Archive.Entries) {
        $name = $entry.FullName
        if (-not $seen.Add($name) -or $name -match '(^|/)\.\.(/|$)|[\\\r\n]' -or $name.StartsWith('/')) {
            throw "Unsafe or duplicate APK entry: $name"
        }
        if ($name.EndsWith('/')) { continue }
        # Apply this before permitted res/ and META-INF/ paths: microphone
        # captures must not bypass the voice-asset allowlist through resources.
        if ($name -match '\.(u|unr|utx|uax|umx|s16|hpvc|pcm|raw|mp2|wav|mp3|ogg|flac|m4a|aac|opus|aif|aiff|amr|caf|wma|webm|mp4|m4v|3gp|jks|keystore|pem|p12)$') {
            throw "Game data, recording or signing secret in APK: $name"
        }
        if ($name.StartsWith('assets/hpvr-voice/', [StringComparison]::Ordinal)) {
            $relative = $name.Substring('assets/hpvr-voice/'.Length)
            if (-not $VoiceBuild -or @($voiceAssets.Keys) -cnotcontains $relative) {
                throw "Unexpected voice asset: $name"
            }
            if ($entry.Length -le 0 -or $entry.Length -gt 8MB) { throw "Invalid voice asset size: $name" }
            $stream = $entry.Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
            finally { $sha.Dispose(); $stream.Dispose() }
            if ($hash -cne $voiceAssets[$relative]) { throw "Voice asset SHA-256 mismatch: $name" }
            continue
        }
        if ($name -notmatch '^(AndroidManifest\.xml|resources\.arsc|classes(?:[0-9]+)?\.dex|lib/arm64-v8a/(?:libhpvr_quest|libopenxr_loader)\.so|META-INF/[^\r\n]+|res/[^\r\n]+)$') {
            throw "Unexpected APK payload (game data must remain outside the APK): $name"
        }
    }
    foreach ($relative in $voiceAssets.Keys) {
        if (-not $seen.Contains("assets/hpvr-voice/$relative")) { throw "Missing licensed voice asset: $relative" }
    }
}
