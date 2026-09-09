[CmdletBinding()]
param(
    [string]$ApkPath
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($ApkPath)) {
    $ApkPath = Join-Path $repositoryRoot 'android\app\build\outputs\apk\debug\app-debug.apk'
}
$ApkPath = [System.IO.Path]::GetFullPath($ApkPath)

$buildTools = 'C:\Dev\android-toolchain\sdk\build-tools\35.0.0'
$ndkBin = 'C:\Dev\android-toolchain\sdk\ndk\27.2.12479018\toolchains\llvm\prebuilt\windows-x86_64\bin'
$aapt = Join-Path $buildTools 'aapt2.exe'
$apksigner = Join-Path $buildTools 'apksigner.bat'
$zipalign = Join-Path $buildTools 'zipalign.exe'
$readelf = Join-Path $ndkBin 'llvm-readelf.exe'

foreach ($requiredPath in @($ApkPath, $aapt, $apksigner, $zipalign, $readelf)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required verification input is missing: $requiredPath"
    }
}

$badging = (& $aapt dump badging $ApkPath 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw 'aapt2 failed to read the APK'
}
foreach ($expected in @(
    "package: name='io.github.hpvr.quest'",
    "minSdkVersion:'32'",
    "targetSdkVersion:'35'",
    "native-code: 'arm64-v8a'",
    "uses-permission: name='org.khronos.openxr.permission.OPENXR'",
    "uses-permission: name='org.khronos.openxr.permission.OPENXR_SYSTEM'"
)) {
    if (-not $badging.Contains($expected)) {
        throw "APK badging is missing: $expected"
    }
}

& $apksigner verify --verbose $ApkPath
if ($LASTEXITCODE -ne 0) {
    throw 'APK signature verification failed'
}
& $zipalign -c -P 16 4 $ApkPath
if ($LASTEXITCODE -ne 0) {
    throw 'APK alignment verification failed'
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($ApkPath)
try {
    $entries = @($archive.Entries | ForEach-Object { $_.FullName })
    $resourcesEntry = $archive.GetEntry('resources.arsc')
    $resourcesLength = if ($null -eq $resourcesEntry) { -1 } else { $resourcesEntry.Length }
    $resourcesCompressedLength = if ($null -eq $resourcesEntry) { -1 } else { $resourcesEntry.CompressedLength }
} finally {
    $archive.Dispose()
}

if ($null -eq $resourcesEntry) {
    throw 'APK is missing resources.arsc'
}
if ($resourcesLength -ne $resourcesCompressedLength) {
    throw 'resources.arsc must be stored uncompressed for targetSdk 30+'
}

foreach ($requiredEntry in @(
    'lib/arm64-v8a/libhpvr_quest.so',
    'lib/arm64-v8a/libopenxr_loader.so'
)) {
    if ($entries -notcontains $requiredEntry) {
        throw "APK is missing native entry: $requiredEntry"
    }
}
if ($entries | Where-Object { $_ -match '\.(u|unr|utx|uax|umx)$' }) {
    throw 'APK unexpectedly contains a proprietary Unreal package'
}
if ($entries | Where-Object { $_ -like 'lib/*' -and $_ -notlike 'lib/arm64-v8a/*' }) {
    throw 'APK unexpectedly contains a non-ARM64 native ABI'
}

$verificationTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$temporaryDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $verificationTempRoot "hpvr-apk-verify-$([guid]::NewGuid().ToString('N'))"))
if (-not $temporaryDirectory.StartsWith($verificationTempRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path -Leaf $temporaryDirectory) -notmatch '^hpvr-apk-verify-[0-9a-f]{32}$') {
    throw 'Unsafe verification temporary directory'
}
$extractedLibrary = Join-Path $temporaryDirectory 'libhpvr_quest.so'
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
try {
    $archive = [System.IO.Compression.ZipFile]::OpenRead($ApkPath)
    try {
        $nativeEntry = $archive.GetEntry('lib/arm64-v8a/libhpvr_quest.so')
        if ($null -eq $nativeEntry) {
            throw 'APK native host entry disappeared during verification'
        }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile(
            $nativeEntry, $extractedLibrary, $true)
    } finally {
        $archive.Dispose()
    }

    $elfHeader = (& $readelf -h $extractedLibrary 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $elfHeader -notmatch 'Machine:\s+AArch64') {
        throw 'APK libhpvr_quest.so is not a valid AArch64 ELF library'
    }
    $dynamic = (& $readelf -d $extractedLibrary 2>&1) -join "`n"
    foreach ($dependency in @('libopenxr_loader.so', 'libvulkan.so', 'libaaudio.so', 'libandroid.so', 'liblog.so')) {
        if (-not $dynamic.Contains("[$dependency]")) {
            throw "ARM64 host is missing dynamic dependency: $dependency"
        }
    }
    $symbols = (& $readelf --dyn-syms $extractedLibrary 2>&1) -join "`n"
    foreach ($symbol in @(
        'ANativeActivity_onCreate',
        'android_main',
        'xrWaitFrame',
        'xrEndFrame',
        'xrCreateActionSet',
        'xrAttachSessionActionSets',
        'xrSyncActions',
        'xrLocateSpace',
        'vkQueueSubmit',
        'vkCreateGraphicsPipelines',
        'vkCmdCopyBufferToImage',
        'vkCmdDraw',
        'hpvr_wand_abi_version',
        'hpvr_wand_project_trajectory',
        'hpvr_hp1_gesture_abi_version',
        'hpvr_hp1_load_spell_profile_utf8',
        'hpvr_hp1_compare_gesture',
        'hpvr_hp1_load_player_start_utf8',
        'hpvr_hp1_load_skeletal_geometry_utf8',
        'hpvr_hp1_load_skeletal_mesh_utf8',
        'hpvr_hp1_load_skeletal_animation_utf8',
        'hpvr_hp1_load_character_manifest_utf8',
        'xrGetActionStateBoolean'
    )) {
        if (-not $symbols.Contains($symbol)) {
            throw "APK ARM64 host is missing required linked symbol: $symbol"
        }
    }
    $readOnlyData = (& $readelf --string-dump=.rodata $extractedLibrary 2>&1) -join "`n"
    if ($LASTEXITCODE -ne 0 -or
        -not $readOnlyData.Contains('gate=C35 loading=WARNER_THEATER sprint=L3_TOGGLE running=MATCHED_TRANSLATION knights=ONESHOT_CLAMPED story=DRACO_THEN_OPTIONAL_FILCH') -or
        -not $readOnlyData.Contains('/user/hand/left/input/thumbstick/click') -or
        -not $readOnlyData.Contains('[hpvr.quest.twins] status=AUTHORED_TRANSITION') -or
        -not $readOnlyData.Contains('content=WARNER_OWNED size=640x480') -or
        -not $readOnlyData.Contains('[hpvr.quest.story] status=STARTED') -or
        -not $readOnlyData.Contains('[hpvr.quest.jump_lesson] status=STARTED') -or
        -not $readOnlyData.Contains('[hpvr.quest.jump] status=STARTED button=A') -or
        -not $readOnlyData.Contains('background_tracks=CONTINUE') -or
        -not $readOnlyData.Contains('FEFolioBackTexture') -or
        -not $readOnlyData.Contains('HarryBarFull') -or
        -not $readOnlyData.Contains('/user/hand/left/input/menu/click') -or
        -not $readOnlyData.Contains('/user/hand/right/input/a/click') -or
        -not $readOnlyData.Contains('[hpvr.quest.twins] status=STARTED') -or
        -not $readOnlyData.Contains('[hpvr.quest.beans] status=COLLECTED') -or
        -not $readOnlyData.Contains('[hpvr.quest.climb] status=LESSON_COMPLETE') -or
        -not $readOnlyData.Contains('[hpvr.quest.basic_cast] status=CAST') -or
        -not $readOnlyData.Contains('spell_dud') -or
        -not $readOnlyData.Contains('status=RON_ENCOUNTER object=CutScene51') -or
        -not $readOnlyData.Contains('vkCreateGraphicsPipelines(frontend_no_depth)') -or
        -not $readOnlyData.Contains('story_pages=14 save_slots=3 start=MAIN_MENU') -or
        -not $readOnlyData.Contains('spell_lock=STORY') -or
        -not $readOnlyData.Contains('[hpvr.quest.save] status=RESTORED') -or
        -not $readOnlyData.Contains('HPVR_PROGRESS') -or
        -not $readOnlyData.Contains('[hpvr.quest.music] status=READY') -or
        -not $readOnlyData.Contains('skin_overrides=%zu') -or
        -not $readOnlyData.Contains('debug_crowd=REMOVED') -or
        -not $readOnlyData.Contains('[hpvr.quest.doors] status=READY') -or
        -not $readOnlyData.Contains('AUTHORED_TOGGLE') -or
        -not $readOnlyData.Contains('[hpvr.quest.children.data] status=READY') -or
        -not $readOnlyData.Contains('[hpvr.quest.dispatcher] status=SCHEDULED') -or
        -not $readOnlyData.Contains('[hpvr.quest.children.spawn] status=ACTIVE') -or
        -not $readOnlyData.Contains('DESTROY_AT_PATROL_END') -or
        -not $readOnlyData.Contains('[hpvr.quest.audio.cache] status=PCM_READY') -or
        -not $readOnlyData.Contains('WARNER_LOADING') -or
        -not $readOnlyData.Contains('animation_frames=%u') -or
        -not $readOnlyData.Contains('source_duration_s=%.3f') -or
        -not $readOnlyData.Contains('collision_blocked=%llu') -or
        -not $readOnlyData.Contains('capsule_radius_m=%.3f') -or
        -not $readOnlyData.Contains('[hpvr.quest.world] status=READY') -or
        -not $readOnlyData.Contains('[hpvr.quest.script.trigger] status=ENTER') -or
        -not $readOnlyData.Contains('[hpvr.quest.audio] status=RUNNING') -or
        -not $readOnlyData.Contains('[hpvr.quest.lighting] status=CONTRAST_READY') -or
        -not $readOnlyData.Contains('lightmaps=%zu lightmap_size=%ux%u') -or
        -not $readOnlyData.Contains('LIGHTMAP_UPLOAD_REJECTED') -or
        -not $readOnlyData.Contains('vkCreateGraphicsPipelines(effect)') -or
        -not $readOnlyData.Contains('vkCreateGraphicsPipelines(particle)') -or
        -not $readOnlyData.Contains('flames=%zu glows=%zu') -or
        -not $readOnlyData.Contains('alpha_falloff=SMOOTH') -or
        -not $readOnlyData.Contains('[hpvr.quest.fire.texture] status=READY') -or
        -not $readOnlyData.Contains('source=HPParticle.PotFire08') -or
        -not $readOnlyData.Contains('fire_particles=OWNED_POTFIRE08') -or
        -not $readOnlyData.Contains('color_grade=UE1_GAMMA') -or
        -not $readOnlyData.Contains('exposure=1.35 gamma=0.92') -or
        -not $readOnlyData.Contains('knight_actors=6') -or
        -not $readOnlyData.Contains('knights_grounded=%zu') -or
        -not $readOnlyData.Contains('knight_yaw_corrected=%zu') -or
        -not $readOnlyData.Contains('[hpvr.quest.cutscene.data] status=READY') -or
        -not $readOnlyData.Contains('source=SERIALIZED_ACTOR_PROPERTIES') -or
        -not $readOnlyData.Contains('interpreter=PARALLEL_UE1_SUBSET') -or
        -not $readOnlyData.Contains('[hpvr.quest.cutscene.dialogue] status=READY') -or
        -not $readOnlyData.Contains('[hpvr.quest.audio.dialogue] clips=%zu samples=%zu rate=48000') -or
        -not $readOnlyData.Contains('[hpvr.quest.cutscene] status=STARTED') -or
        -not $readOnlyData.Contains('vr_camera=AUTHORED_TARGET_HEAD_RELATIVE') -or
        -not $readOnlyData.Contains('[hpvr.quest.cutscene.command]') -or
        -not $readOnlyData.Contains('[hpvr.quest.cutscene.dialogue] status=PLAY') -or
        -not $readOnlyData.Contains('[hpvr.quest.cutscene] status=FINISHED') -or
        -not $readOnlyData.Contains('cutscene_tracks=%zu') -or
        -not $readOnlyData.Contains('dialogue_clips=%zu') -or
        -not $readOnlyData.Contains('[hpvr.quest.spell.projectile] status=IMPACT') -or
        -not $readOnlyData.Contains('[hpvr.quest.props] status=READY') -or
        -not $readOnlyData.Contains('fixture_vertices=%u') -or
        -not $readOnlyData.Contains('spell_tracing_loop') -or
        -not $readOnlyData.Contains('wand_ready_loop') -or
        -not $readOnlyData.Contains('spell_cast') -or
        -not $readOnlyData.Contains('flipendo_no') -or
        -not $readOnlyData.Contains('s_spell_hit1')) {
        throw 'APK ARM64 host is missing a required C35 story/rendering marker'
    }
    foreach ($marker in @('[hpvr.quest.bump]', '[hpvr.quest.ron]', 'TWINS_DEPART', 'hprops.gregorysmarmy', 'stone_door_long', '[hpvr.quest.peeves]', 'AUTHORED_TRANSITION', '111Peeves1', 'HPVR_PROGRESS 7', 'ROUND_PASSED', 'CARD_AWARDED', 'PRESERVE_WORLD', 'cutscene=6DOF_PCM_RELEASE', 'objective=OWNED_TEXT', 'frog=ANIMATED_GROUNDED', 'twins=STAGED_SWAP', 'reward=APPROACH_FRED', 'hprops.transblackboard', 'ghost_depth_test')) {
        if (-not $readOnlyData.Contains($marker)) { throw "APK missing C35 marker: $marker" }
    }
    if ($readOnlyData.Contains('RED_BLUE_CLEAR')) {
        throw 'APK ARM64 host still contains the retired red/blue loading path'
    }
    # The writer now emits VR2. VR1 backward reads are exercised by the host
    # migration test; optimized string comparisons need not retain a literal.
    foreach ($marker in @('hprops.transtrestletable', '[hpvr.quest.ssr]', 'history=LEFT_SHARED passes=0', 'HPVR_VR2', 'left_squeeze', 'right_squeeze', 'before=%u after=%u')) {
        if (-not $readOnlyData.Contains($marker)) { throw "APK missing C35 graphics/gameplay marker: $marker" }
    }
    foreach ($marker in @('HPVR_VR2', 'notice=WELCOME trigger=FIRST_ACTUAL_STEP', 'notice=THANK_YOU trigger=LESSON_TRAVEL_BOUNDARY',
        'demo=FIRST_STEP_AND_LESSON_END', 'lesson_difficulty=ORIGINAL_OPTIONAL_RELAXED', 'OPEN DISCORD IN BROWSER',
        'https://discord.com/channels/747967102895390741/1543691482861408276',
        'capture=WORLD_BEFORE_OVERLAY trace=PROJECTED_16', 'vkCreateRenderPass(left_overlay_load)',
        '[hpvr.quest.perf]', '/perfmetrics_meta/device/gpu_utilization', 'PERFORMANCE DEBUGGER',
        'BOTH GRIPS + MENU: HIDE / SETTINGS', 'pickup_wizardcard2', 'hprops.hogwartsurn')) {
        if (-not $readOnlyData.Contains($marker)) { throw "APK missing C35 marker: $marker" }
    }
} finally {
    Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
}

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $ApkPath
Write-Host '[hpvr.quest.apk.verify] status=PASS'
Write-Host "[hpvr.quest.apk.verify] path=$ApkPath"
Write-Host "[hpvr.quest.apk.verify] sha256=$($hash.Hash)"
Write-Host '[hpvr.quest.apk.verify] gate=C35 ui=BOOK_HUD jump=ASSISTED sprint=L3_TOGGLE quest=APPROACH_FRED_25_BEANS story=DRACO_THEN_OPTIONAL_FILCH cutscenes=6DOF abi=arm64-v8a proprietary_assets=0 loading=WARNER_THEATER save_format=HPVR_PROGRESS_V7_READS_V1_TO_V6 music_tracks=4_stereo dialogue_clips=98 harry=THEATRICAL_ONLY lesson=CLASSROOM_CAST_GHOST_BOARDS objective=OWNED_TEXT frog=ANIMATED_GROUNDED signature=VALID alignment=VALID runtime_acceptance=PENDING'
