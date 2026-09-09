#requires -Version 5.1
[CmdletBinding()]
param([string]$FixtureRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'INSTALL-HPVR.ps1') -LibraryOnly
if (-not $FixtureRoot) { $FixtureRoot=Join-Path ([IO.Path]::GetTempPath()) ('hpvr-ffmpeg-tests-'+[Guid]::NewGuid().ToString('N')) }
$fixture=Get-HpvrFullPath $FixtureRoot
if(Test-Path -LiteralPath $fixture){throw 'Use a fresh fixture directory.'}
Assert-HpvrNoLinks $fixture
New-Item -ItemType Directory -Path $fixture | Out-Null
$script:checks=0
function Check([bool]$ok,[string]$name){if(-not $ok){throw "Failed: $name"};++$script:checks}
function Reject([scriptblock]$action,[string]$name){$failed=$false;try{& $action | Out-Null}catch{$failed=$true};Check $failed $name}
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$zip=Join-Path $fixture 'fixture.zip'
$fakeExe=[byte[]](77,90,1,2,3,4,5)
$exeSource=Join-Path $fixture 'local.exe'
[IO.File]::WriteAllBytes($exeSource,$fakeExe)
$archive=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Create)
try {
 foreach($name in @('test/bin/ffmpeg.exe','test/LICENSE','test/README.txt','../outside.exe')){
  $entry=$archive.CreateEntry($name);$stream=$entry.Open()
  try{$stream.Write($fakeExe,0,$fakeExe.Length)}finally{$stream.Dispose()}
 }
}finally{$archive.Dispose()}
$script:spec=[pscustomobject]@{Version='test';Url='https://example.invalid/test.zip';Prefix='test/';
 ArchiveSha256=(Get-FileHash $zip -Algorithm SHA256).Hash;ExeSha256=(Get-FileHash $exeSource -Algorithm SHA256).Hash}
function Get-HpvrFfmpegSpec {return $script:spec}
$script:downloads=0;$script:failNetwork=$false
function Save-HpvrFfmpegDownload([string]$Url,[string]$Destination){
 ++$script:downloads
 if($script:failNetwork){throw 'Synthetic network outage'}
 Copy-Item -LiteralPath $zip -Destination $Destination
}
$oldPath=$env:PATH
try {
 $env:PATH=''
 $cache=Join-Path $fixture 'cache with spaces'
 Check ((Get-HpvrFfmpeg $exeSource @() $cache) -eq $exeSource) 'Explicit path wins'
 Check ((Get-HpvrFfmpeg '' @($exeSource) $cache) -eq $exeSource) 'Installed candidate wins'
 Check ($script:downloads -eq 0) 'Installed tool requires no network'
 Reject {Get-HpvrFfmpeg (Join-Path $fixture 'missing.exe') @() $cache} 'Invalid explicit path does not download'
 Reject {Get-HpvrFfmpeg '' @() $cache -NoDownload} 'Offline missing tool is actionable'
 Check ($script:downloads -eq 0) 'Offline path did not use network'
 $result=Get-HpvrFfmpeg '' @() $cache
 Check ((Get-FileHash $result -Algorithm SHA256).Hash -eq $script:spec.ExeSha256) 'Verified executable extracted'
 Check ($script:downloads -eq 1) 'One automatic download'
 Check ((Test-Path (Join-Path $cache 'LICENSE')) -and (Test-Path (Join-Path $cache 'README.txt'))) 'License and README preserved'
 Check (-not (Test-Path (Join-Path $fixture 'outside.exe'))) 'Arbitrary ZIP paths never extracted'
 Check (@(Get-ChildItem $cache -File | Where-Object {$_.Name -match '\.zip$|\.pending$'}).Count -eq 0) 'Temporary files cleaned'
 Check ((Get-HpvrFfmpeg '' @() $cache -NoDownload) -eq $result) 'Verified cache works offline'
 Check ($script:downloads -eq 1) 'Cache avoids another download'
 [IO.File]::WriteAllBytes($result,[byte[]](9))
 Reject {Get-HpvrFfmpeg '' @() $cache -NoDownload} 'Tampered cache rejected offline'
 $null=Get-HpvrFfmpeg '' @() $cache
 Check ($script:downloads -eq 2 -and (Get-FileHash $result -Algorithm SHA256).Hash -eq $script:spec.ExeSha256) 'Tampered cache repaired online'
 $goodArchiveHash=$script:spec.ArchiveSha256
 $script:spec.ArchiveSha256='0'*64
 $badCache=Join-Path $fixture 'bad archive'
 Reject {Get-HpvrFfmpeg '' @() $badCache} 'Wrong archive hash fails closed'
 Check (-not (Test-Path (Join-Path $badCache 'ffmpeg.exe'))) 'Unverified archive not admitted'
 $script:spec.ArchiveSha256=$goodArchiveHash
 $goodExeHash=$script:spec.ExeSha256;$script:spec.ExeSha256='0'*64
 Reject {Get-HpvrFfmpeg '' @() (Join-Path $fixture 'bad exe')} 'Wrong executable hash fails closed'
 $script:spec.ExeSha256=$goodExeHash
 $script:spec.Prefix='missing/'
 Reject {Get-HpvrFfmpeg '' @() (Join-Path $fixture 'missing entry')} 'Missing expected ZIP entry rejected'
 $script:spec.Prefix='test/'
 $script:failNetwork=$true
 Reject {Get-HpvrFfmpeg '' @() (Join-Path $fixture 'offline')} 'Network failure reports manual fallback'
 Check ([string]::IsNullOrEmpty($env:PATH)) 'No PATH modification'
}finally{$env:PATH=$oldPath}
Write-Output "FFMPEG_BOOTSTRAP_TESTS=PASS checks=$script:checks network=MOCKED device_actions=0 fixture=$fixture"
