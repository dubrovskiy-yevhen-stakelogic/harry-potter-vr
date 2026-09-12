[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$scriptPath = Join-Path $PSScriptRoot 'BUILD-NEURAL-VOICE-RUNTIME.ps1'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
# Load only pure validators, never the downloader, compiler, or runtime installer.
foreach ($name in @('Assert-SafePath','Assert-Hash','Assert-Runtime')) {
    $definition = $ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true)
    if (-not $definition) { throw "Missing validator: $name" }
    . ([ScriptBlock]::Create($definition.Extent.Text))
}
$root = Join-Path $repo ('local/neural-runtime-tests-' + [Guid]::NewGuid().ToString('N'))
$directory = Join-Path $root 'runtime'
[void](New-Item -ItemType Directory -Path $directory)
$commit = '917bed95c8e5c7c18aa4d69fea42e9ef8ef0a60e'
$sourceHash = 'ACF539E930283442C4237B7B23A06EBE3BFF10CBC00694A4F09A3580E3C10E9E'
$ortHash = 'B36CEC051D8CAD6EC7ED3120B03ADC9AAD006E3D4B985AF5AC0EEB99504E2D99'
$patchAfter = 'C27AD8463D43852893F110BD2BDD86C0498CAA4DFA784CE87B2CA29EEED8D058'
$libraries = @('libsherpa-onnx-c-api.a','libsherpa-onnx-core.a','libkaldi-decoder-core.a',
    'libsherpa-onnx-kaldifst-core.a','libsherpa-onnx-fstfar.a','libsherpa-onnx-fst.a',
    'libkaldi-native-fbank-core.a','libkissfft-float.a','libonnxruntime.a','libssentencepiece_core.a')
$utf8 = [Text.UTF8Encoding]::new($false)
$fileNames = @('include/sherpa-onnx/c-api/c-api.h') + @($libraries | ForEach-Object { "lib/$_" })
$files = @(foreach ($name in $fileNames) {
    $file = Join-Path $directory $name
    [void](New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($file)) -Force)
    [IO.File]::WriteAllText($file,'fixture',$utf8)
    @{path=$name;sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash}
})
$manifest = [ordered]@{schema=1;sherpaCommit=$commit;sourceSha256=$sourceHash;ortVersion='1.27.1';ortSha256=$ortHash;
    patchSha256=$patchAfter;abi='arm64-v8a';androidApi=32;ndk='27.2.12479018';stl='c++_static';tts=$false;jni=$false;files=$files}
$manifestPath = Join-Path $directory 'runtime-manifest.json'
$baseline = $manifest | ConvertTo-Json -Depth 5
function Write-Manifest($Value) { [IO.File]::WriteAllText($manifestPath,($Value | ConvertTo-Json -Depth 5),$utf8) }
function Assert-Rejected([string]$Name,[scriptblock]$Action) {
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    if (-not $rejected) { throw "Validator accepted unsafe case: $Name" }
    Write-Host "PASS: $Name"
}
Write-Manifest $manifest
Assert-Runtime $directory
Write-Host 'PASS: complete pinned runtime'
Assert-Rejected 'path traversal outside private root' { Assert-SafePath (Join-Path $root '../outside') }
foreach ($field in @('sherpaCommit','sourceSha256','ortVersion','ortSha256','patchSha256','abi','androidApi','ndk','stl','tts','jni')) {
    $bad = $baseline | ConvertFrom-Json
    if ($field -in @('tts','jni')) { $bad.$field = $true }
    elseif ($field -eq 'androidApi') { $bad.$field = 31 }
    else { $bad.$field = 'wrong' }
    Write-Manifest $bad
    Assert-Rejected "changed build identity: $field" { Assert-Runtime $directory }
}
$bad = $baseline | ConvertFrom-Json
$bad.files[0].sha256 = '0' * 64
Write-Manifest $bad
Assert-Rejected 'changed binary hash' { Assert-Runtime $directory }
$bad = $baseline | ConvertFrom-Json
$bad.files = @($bad.files) + @($bad.files[0])
Write-Manifest $bad
Assert-Rejected 'duplicate manifest entry' { Assert-Runtime $directory }
$bad = $baseline | ConvertFrom-Json
$bad.files[0].path = '../outside'
Write-Manifest $bad
Assert-Rejected 'manifest path traversal' { Assert-Runtime $directory }
$bad = $baseline | ConvertFrom-Json
$bad.files = @($bad.files | Select-Object -Skip 1)
Write-Manifest $bad
Assert-Rejected 'missing header' { Assert-Runtime $directory }
Write-Manifest ($baseline | ConvertFrom-Json)
[IO.File]::WriteAllText((Join-Path $directory 'unexpected.bin'),'fixture',$utf8)
Assert-Rejected 'unknown unowned file' { Assert-Runtime $directory }
Write-Host "Native runtime validator checks passed. Isolated fixture preserved: $root"
