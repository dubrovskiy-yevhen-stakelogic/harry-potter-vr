[CmdletBinding()]
param(
    [switch]$InitializeSigningKey,
    [string]$OutputDirectory,
    [string]$AndroidSdk = 'C:\Dev\android-toolchain\sdk',
    [string]$JavaDirectory = 'C:\Dev\android-toolchain\jdk21',
    [string]$Gradle,
    [switch]$AllowDependencyDownload
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$androidRoot = Join-Path $repositoryRoot 'android'
function Get-HpvrBuildMetadata([string]$Path) {
    $source = Get-Content -LiteralPath $Path -Raw
    $source = [regex]::Replace($source, '(?s)/\*.*?\*/', '')
    $codes = [regex]::Matches($source, '(?m)^\s*versionCode\b[^\r\n]*')
    $names = [regex]::Matches($source, '(?m)^\s*versionName\b[^\r\n]*')
    if ($codes.Count -ne 1 -or $names.Count -ne 1 -or
        $codes[0].Value -notmatch '^\s*versionCode\s+([0-9]+)\s*(?://[^\r\n]*)?$') {
        throw 'build.gradle must declare exactly one literal versionCode and versionName.'
    }
    $code = 0
    if (-not [int]::TryParse($Matches[1], [ref]$code) -or $code -lt 1 -or $code -gt 2100000000) {
        throw 'Invalid Android versionCode.'
    }
    if ($names[0].Value -notmatch '^\s*versionName\s+([''"])([A-Za-z0-9][A-Za-z0-9._+-]{0,95})\1\s*(?://[^\r\n]*)?$') {
        throw 'versionName must be a single literal containing filename-safe characters.'
    }
    return [pscustomobject]@{VersionCode=$code;VersionName=$Matches[2]}
}
$gradleMetadataPath = Join-Path $androidRoot 'app\build.gradle'
$version = Get-HpvrBuildMetadata $gradleMetadataPath
$voiceBuild = $version.VersionCode -ge 44
$metadataHash = (Get-FileHash -LiteralPath $gradleMetadataPath -Algorithm SHA256).Hash
$apkFileName = "HPVR-Quest-$($version.VersionName).apk"
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = 'artifacts\quest-release-' + $version.VersionName + '-c' + $version.VersionCode + '-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
}
$buildRoot = Join-Path $repositoryRoot 'build'
$signingRoot = Join-Path $repositoryRoot 'local\signing\release'
$keystore = Join-Path $signingRoot 'hpvr-release.p12'
$credentialPath = Join-Path $signingRoot 'hpvr-release-password.clixml'
$alias = 'hpvr-release'
$OutputDirectory = [System.IO.Path]::GetFullPath($(if([System.IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $repositoryRoot $OutputDirectory}))
$artifactRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'artifacts'))
if (-not $OutputDirectory.StartsWith($artifactRoot + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'The release output directory must be a child of this repository\artifacts.'
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw 'Release output already exists. Use a new -OutputDirectory; previous releases are never overwritten.'
}

if ([string]::IsNullOrWhiteSpace($Gradle)) {
    $cachedGradle = Join-Path $env:USERPROFILE '.gradle\wrapper\dists\gradle-9.3.1-bin'
    $candidates = @(Get-ChildItem -LiteralPath $cachedGradle -Filter gradle.bat -Recurse -File -ErrorAction SilentlyContinue)
    if ($candidates.Count -ne 1) {
        throw 'Expected one cached Gradle 9.3.1 distribution. Pass its bin\gradle.bat as -Gradle.'
    }
    $Gradle = $candidates[0].FullName
}
$buildTools = Join-Path $AndroidSdk 'build-tools\35.0.0'
$keytool = Join-Path $JavaDirectory 'bin\keytool.exe'
$zipalign = Join-Path $buildTools 'zipalign.exe'
$apksigner = Join-Path $buildTools 'apksigner.bat'
$aapt = Join-Path $buildTools 'aapt.exe'
foreach ($required in @($Gradle, $keytool, $zipalign, $apksigner, $aapt,
        (Join-Path $AndroidSdk 'ndk\27.2.12479018\build\cmake\android.toolchain.cmake'))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required release tool is missing: $required"
    }
}

$hasKey = Test-Path -LiteralPath $keystore -PathType Leaf
$hasCredential = Test-Path -LiteralPath $credentialPath -PathType Leaf
if ($hasKey -ne $hasCredential) {
    throw 'Incomplete release signing identity. Restore the matching key and DPAPI credential; never silently generate a new identity.'
}
if (-not $hasKey -and -not $InitializeSigningKey) {
    throw 'No release identity exists. For the first release only, pass -InitializeSigningKey. Keep local\signing\release private and backed up.'
}

$savedEnvironment = @{}
foreach ($name in @('JAVA_HOME', 'ANDROID_HOME', 'ANDROID_SDK_ROOT', 'HPVR_RELEASE_STORE_PASSWORD')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$packageDirectory = Join-Path $buildRoot ('quest-release-package-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $packageDirectory | Out-Null
try {
    $env:JAVA_HOME = $JavaDirectory
    $env:ANDROID_HOME = $AndroidSdk
    $env:ANDROID_SDK_ROOT = $AndroidSdk

    if (-not $hasKey) {
        New-Item -ItemType Directory -Path $signingRoot -Force | Out-Null
        # Ignored private directory: only this Windows account and SYSTEM.
        $acl = New-Object System.Security.AccessControl.DirectorySecurity
        $acl.SetAccessRuleProtection($true, $false)
        foreach ($sid in @([System.Security.Principal.WindowsIdentity]::GetCurrent().User,
                (New-Object System.Security.Principal.SecurityIdentifier('S-1-5-18')))) {
            $rule = New-Object System.Security.AccessControl.FileSystemAccessRule(
                $sid, 'FullControl', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
            $acl.AddAccessRule($rule)
        }
        Set-Acl -LiteralPath $signingRoot -AclObject $acl
        $randomBytes = New-Object byte[] 48
        $random = [System.Security.Cryptography.RandomNumberGenerator]::Create()
        try { $random.GetBytes($randomBytes) } finally { $random.Dispose() }
        $plainPassword = [Convert]::ToBase64String($randomBytes)
        $securePassword = ConvertTo-SecureString -String $plainPassword -AsPlainText -Force
        $credential = New-Object System.Management.Automation.PSCredential($alias, $securePassword)
        # Export-Clixml protects SecureString with Windows DPAPI, not base64.
        $credential | Export-Clixml -LiteralPath $credentialPath
        $env:HPVR_RELEASE_STORE_PASSWORD = $plainPassword
        $plainPassword = $null
        [Array]::Clear($randomBytes, 0, $randomBytes.Length)
        $keyArguments = @('-genkeypair', '-noprompt', '-alias', $alias, '-keyalg', 'RSA', '-keysize', '4096',
            '-sigalg', 'SHA256withRSA', '-validity', '10000', '-dname', 'CN=HPVR Demo Release',
            '-storetype', 'PKCS12', '-keystore', $keystore,
            '-storepass:env', 'HPVR_RELEASE_STORE_PASSWORD', '-keypass:env', 'HPVR_RELEASE_STORE_PASSWORD')
        & $keytool @keyArguments
        if ($LASTEXITCODE -ne 0) { throw 'Release key generation failed. Preserve partial files and investigate before trying again.' }
        Write-Host '[hpvr.quest.release.signing] identity=CREATED password=WINDOWS_DPAPI private=local/signing/release'
    } else {
        $credential = Import-Clixml -LiteralPath $credentialPath
        if ($credential -isnot [System.Management.Automation.PSCredential] -or $credential.UserName -ne $alias) {
            throw 'Invalid release signing credential.'
        }
        $env:HPVR_RELEASE_STORE_PASSWORD = $credential.GetNetworkCredential().Password
    }
    & $keytool -list -alias $alias -keystore $keystore -storepass:env HPVR_RELEASE_STORE_PASSWORD | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Release keystore/password validation failed.' }
    # Gradle never receives signing secrets. Build the real Release variant
    # from C++/GLSL source, not an old debug APK with a replaced library.
    Remove-Item Env:HPVR_RELEASE_STORE_PASSWORD -ErrorAction SilentlyContinue
    if ($voiceBuild) {
        & (Join-Path $repositoryRoot 'tools/voice/FETCH-VOICE-DEPENDENCIES.ps1') -Offline:(-not $AllowDependencyDownload)
        & (Join-Path $repositoryRoot 'tools/voice/BUILD-NEURAL-VOICE-RUNTIME.ps1') -Offline:(-not $AllowDependencyDownload)
    }
    $gradleArguments = @('--no-daemon', '--console=plain', '--max-workers=6')
    if (-not $AllowDependencyDownload) { $gradleArguments += '--offline' }
    $gradleArguments += @('-p', $androidRoot, ':app:assembleRelease')
    & $Gradle @gradleArguments
    if ($LASTEXITCODE -ne 0) { throw "Release source build failed: $LASTEXITCODE" }
    if ((Get-FileHash -LiteralPath $gradleMetadataPath -Algorithm SHA256).Hash -ne $metadataHash) {
        throw 'build.gradle changed during the build; metadata must be captured from one consistent source revision.'
    }

    $unsignedApk = Join-Path $androidRoot 'app\build\outputs\apk\release\app-release-unsigned.apk'
    if (-not (Test-Path -LiteralPath $unsignedApk -PathType Leaf)) { throw 'Gradle did not produce an unsigned Release APK.' }
    $releaseCaches = @(Get-ChildItem -LiteralPath (Join-Path $androidRoot 'app\.cxx\Release') -Filter CMakeCache.txt -Recurse -File |
        Where-Object { $_.FullName -match '[\\/]arm64-v8a[\\/]' })
    if ($releaseCaches.Count -eq 0) { throw 'No ARM64 Release CMake cache was produced.' }
    foreach ($cache in $releaseCaches) {
        if (-not (Select-String -LiteralPath $cache.FullName -Pattern '^CMAKE_BUILD_TYPE:STRING=Release$' -Quiet)) {
            throw "A Release native cache has the wrong build type: $($cache.FullName)"
        }
        # AGP can leave CMAKE_CXX_FLAGS_RELEASE empty in the cache while the
        # Android toolchain supplies Release flags. Check actual invocations.
        $commandsPath = Join-Path $cache.DirectoryName 'compile_commands.json'
        $commands = @(Get-Content -LiteralPath $commandsPath -Raw | ConvertFrom-Json)
        if ($commands.Count -eq 0) { throw 'Release compile command evidence is empty.' }
        foreach ($compile in $commands) {
            if ($compile.command -notmatch '(?:^|\s)-O[23s](?:\s|$)' -or
                $compile.command -notmatch '(?:^|\s)-DNDEBUG(?:\s|$)' -or
                $compile.command -match '(?:^|\s)-O0(?:\s|$)') {
                throw "Release source was not compiled with optimized/NDEBUG flags: $($compile.file)"
            }
        }
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    . (Join-Path $repositoryRoot 'tools/voice/VOICE-APK-PAYLOAD.ps1')
    $archive = [System.IO.Compression.ZipFile]::OpenRead($unsignedApk)
    try {
        Assert-HpvrApkPayload $archive $voiceBuild (Join-Path $repositoryRoot 'tools/voice/VOICE-ASSETS.psd1')
        foreach ($native in @('lib/arm64-v8a/libhpvr_quest.so', 'lib/arm64-v8a/libopenxr_loader.so')) {
            if ($null -eq $archive.GetEntry($native)) { throw "Missing ARM64 native library: $native" }
        }
    } finally { $archive.Dispose() }

    # Preserve exact license texts from the pinned dependencies. The AAR
    # license and full NDK notices are build inputs, not downloaded game data.
    $loaderCache = Join-Path $env:USERPROFILE '.gradle\caches\modules-2\files-2.1\org.khronos.openxr\openxr_loader_for_android\1.1.43'
    $loaderAars = @(Get-ChildItem -LiteralPath $loaderCache -Filter '*.aar' -Recurse -File)
    if ($loaderAars.Count -ne 1) { throw 'Expected one pinned OpenXR loader AAR for its license text.' }
    $notices = Join-Path $packageDirectory 'THIRD-PARTY'
    New-Item -ItemType Directory -Path $notices | Out-Null
    $loaderArchive = [System.IO.Compression.ZipFile]::OpenRead($loaderAars[0].FullName)
    try {
        $license = $loaderArchive.GetEntry('META-INF/LICENSE')
        if ($null -eq $license) { throw 'Pinned OpenXR AAR has no license text.' }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($license, (Join-Path $notices 'OpenXR-1.1.43-LICENSE.txt'))
    } finally { $loaderArchive.Dispose() }
    $ndkRoot = Join-Path $AndroidSdk 'ndk\27.2.12479018'
    Copy-Item -LiteralPath (Join-Path $ndkRoot 'NOTICE') -Destination (Join-Path $notices 'NDK-27.2.12479018-NOTICE.txt')
    Copy-Item -LiteralPath (Join-Path $ndkRoot 'NOTICE.toolchain') -Destination (Join-Path $notices 'NDK-27.2.12479018-NOTICE.toolchain.txt')
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'docs\THIRD-PARTY-NOTICES.md') -Destination $notices
    if ($voiceBuild) {
        $voiceAssets = Join-Path $repositoryRoot 'local/neural-voice-dependencies/assets/hpvr-voice'
        $voiceManifest = Import-PowerShellDataFile -LiteralPath (Join-Path $repositoryRoot 'tools/voice/VOICE-ASSETS.psd1')
        $voiceNotices = @{}
        foreach ($relative in $voiceManifest.Keys) {
            if ($relative -match '^(LICENSE-|NOTICE-|EIGEN-|MODEL-README)') {
                $voiceNotices[$relative] = "Voice-$relative"
            }
        }
        foreach ($relative in $voiceNotices.Keys) {
            $noticeSource = Join-Path $voiceAssets $relative
            if ((Get-FileHash -LiteralPath $noticeSource -Algorithm SHA256).Hash -cne $voiceManifest[$relative]) {
                throw "Pinned voice license changed: $relative"
            }
            $noticeOutput = Join-Path $notices $voiceNotices[$relative]
            Copy-Item -LiteralPath $noticeSource -Destination $noticeOutput
            if ((Get-FileHash -LiteralPath $noticeOutput -Algorithm SHA256).Hash -cne $voiceManifest[$relative]) {
                throw "Voice license copy mismatch: $relative"
            }
        }
    }
    $licensedApk = Join-Path $packageDirectory 'licensed-unsigned.apk'
    Copy-Item -LiteralPath $unsignedApk -Destination $licensedApk
    $archive = [System.IO.Compression.ZipFile]::Open($licensedApk, [System.IO.Compression.ZipArchiveMode]::Update)
    try {
        foreach ($notice in Get-ChildItem -LiteralPath $notices -File) {
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $notice.FullName,
                ('META-INF/HPVR-NOTICES/' + $notice.Name), [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $archive.Dispose() }
    $alignedApk = Join-Path $packageDirectory 'aligned.apk'
    $signedApk = Join-Path $packageDirectory $apkFileName
    & $zipalign -f -P 16 4 $licensedApk $alignedApk
    if ($LASTEXITCODE -ne 0) { throw 'Release APK alignment failed.' }
    $env:HPVR_RELEASE_STORE_PASSWORD = $credential.GetNetworkCredential().Password
    & $apksigner sign --ks $keystore --ks-key-alias $alias --ks-pass env:HPVR_RELEASE_STORE_PASSWORD --key-pass env:HPVR_RELEASE_STORE_PASSWORD --out $signedApk $alignedApk
    if ($LASTEXITCODE -ne 0) { throw 'Release APK signing failed.' }
    Remove-Item Env:HPVR_RELEASE_STORE_PASSWORD -ErrorAction SilentlyContinue
    $signatureOutput = @(& $apksigner verify --verbose --print-certs $signedApk)
    if ($LASTEXITCODE -ne 0) { throw 'Release APK signature verification failed.' }
    $signatureOutput | ForEach-Object { Write-Host $_ }
    $certificateLine = @($signatureOutput | Where-Object { $_ -match '^Signer #1 certificate SHA-256 digest: ([0-9a-fA-F]{64})$' })
    if ($certificateLine.Count -ne 1) { throw 'Could not verify the release certificate fingerprint.' }
    $certificateHash = ($certificateLine[0] -split ': ', 2)[1].ToUpperInvariant()
    & $zipalign -c -P 16 4 $signedApk
    if ($LASTEXITCODE -ne 0) { throw 'Signed Release APK alignment verification failed.' }
    $badging = @(& $aapt dump badging $signedApk)
    if ($LASTEXITCODE -ne 0) { throw 'Release APK manifest verification failed.' }
    if ($badging -match '^application-debuggable') { throw 'Refusing to publish a debuggable APK.' }
    $expectedPackage = "^package: name='io\.github\.hpvr\.quest' versionCode='$($version.VersionCode)' versionName='" + [regex]::Escape($version.VersionName) + "'(?:\s|$)"
    if (@($badging | Where-Object { $_ -match $expectedPackage }).Count -ne 1) {
        throw 'Release package id or version differs from the captured build.gradle metadata.'
    }
    if (-not ($badging -match "^native-code: 'arm64-v8a'\s*$")) { throw 'Release must contain ARM64 only.' }

    & (Join-Path $repositoryRoot 'VERIFY-QUEST-APK.ps1') -ApkPath $signedApk -Release -ExpectedVersionCode $version.VersionCode -ExpectedVersionName $version.VersionName -AndroidSdk $AndroidSdk
    New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
    $outputApk = Join-Path $OutputDirectory $apkFileName
    Copy-Item -LiteralPath $signedApk -Destination $outputApk
    Copy-Item -LiteralPath $notices -Destination (Join-Path $OutputDirectory 'THIRD-PARTY') -Recurse
    $hash = (Get-FileHash -LiteralPath $outputApk -Algorithm SHA256).Hash
    "$hash  $apkFileName" | Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt') -Encoding ASCII
    [ordered]@{
        package = 'io.github.hpvr.quest'; version = $version.VersionName; versionCode = $version.VersionCode
        buildType = 'Release'; debuggable = $false; abi = 'arm64-v8a'
        apkSha256 = $hash; certificateSha256 = $certificateHash
        gradleMetadataSha256 = $metadataHash
        gameAssetsIncluded = $false; voiceModelIncluded = $voiceBuild; installed = $false; launched = $false
        builtUtc = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'RELEASE-METADATA.json') -Encoding UTF8
    Write-Host "[hpvr.quest.release] status=PASS native=RELEASE debuggable=0 game_assets=0 voice_model=$([int]$voiceBuild) install=NOT_PERFORMED launch=NOT_PERFORMED"
    Write-Host "[hpvr.quest.release] apk=$outputApk"
    Write-Host "[hpvr.quest.release] sha256=$hash certificate_sha256=$certificateHash"
    Write-Host '[hpvr.quest.release] debug_upgrade=INCOMPATIBLE_SIGNATURE do_not_uninstall_existing_developer_build'
} finally {
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
    $credential = $null
    # Scoped intermediates are retained for diagnostics. They hold APKs only,
    # never a private key or a plaintext signing password.
}
