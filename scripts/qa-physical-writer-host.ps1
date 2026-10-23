#requires -Version 7.2
[CmdletBinding()]
param(
 [Parameter(Mandatory)][ValidateSet('Win32','x64')][string]$Architecture,
 [Parameter(Mandatory)][string]$ComponentDll,
 [Parameter(Mandatory)][string]$SharedActionRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -cne 'true' -or
   -not (Test-Path -LiteralPath $env:RUNNER_TEMP -PathType Container)) {
 throw 'Disposable GitHub Actions runner required'
}
if(@(Get-Process foobar2000 -ErrorAction SilentlyContinue).Count -ne 0) {
 throw 'Unrelated foobar process exists; refusing QA'
}
. (Join-Path $SharedActionRoot 'pinned-installer-cache.ps1')
$sha=if($Architecture -eq 'Win32') {
 '849BC4424326DA81049746E9A2255A3A707E56B76CC69ED11714ED23C69ECE52'
} else {
 '72FE15A88B085EE4E49D68114E2BB9655000C2E0FDF40DCC128BBEF8BCAF3600'
}
$name=if($Architecture -eq 'Win32'){'foobar2000_v2.26.1.exe'}else{'foobar2000-x64_v2.26.1.exe'}
$upstream='https://www.foobar2000.org/files/'+$name
$mirror='https://github.com/lxsdd/dev-infrastructure-public/releases/download/foobar-ci-upstream-v2.26.1/'+$name
$component=(Resolve-Path -LiteralPath $ComponentDll -ErrorAction Stop).Path
if([IO.Path]::GetFileName($component) -cne 'foo_music_metadata_studio.dll') {
 throw 'Unexpected QA component name'
}
$work=Join-Path $env:RUNNER_TEMP ('mms-qa-write-'+[guid]::NewGuid().ToString('N'))
$hostDir=Join-Path $work 'host'
$source=Join-Path $work 'mms-writer-qa.flac'
$original=Join-Path $work 'original.flac'
$after=Join-Path $work 'after-first.flac'
$exe=Join-Path $hostDir 'foobar2000.exe'
$proc=$null
try {
 New-Item -ItemType Directory -Path $work -ErrorAction Stop | Out-Null
 $test=Join-Path $SharedActionRoot 'synthetic-discogs-flac.py'
 & python $test $source
 if($LASTEXITCODE -ne 0) {throw 'Cannot create isolated QA FLAC'}
 Copy-Item -LiteralPath $source -Destination $original -ErrorAction Stop
 $installer=Get-FoobarPinnedInstaller -Architecture $Architecture -Version '2.26.1' -Url $upstream -Sha256 $sha -CacheRoot $env:RUNNER_TEMP -MirrorUrl $mirror
 $p=Start-Process -FilePath $installer -ArgumentList @('/S',"/D=$hostDir") -Wait -PassThru
 if($p.ExitCode -ne 0 -or -not (Test-Path $exe)) {
    throw 'Pinned foobar installer failed'
 }
 $dest=Join-Path $hostDir 'components'
 New-Item -ItemType Directory -Path $dest -Force | Out-Null
 Copy-Item -LiteralPath $component -Destination (Join-Path $dest 'foo_music_metadata_studio.dll')
 New-Item -ItemType File -Path (Join-Path $hostDir 'portable_mode_enabled') | Out-Null
 # The QA component will refuse any source other than this exact path.
 $env:MMS_QA_WRITE_SOURCE=$source
 $env:MMS_QA_REPORT_PATH=Join-Path $work 'mms-writer-report.txt'
 $proc=Start-Process -FilePath $exe -WorkingDirectory $hostDir -PassThru
 Start-Sleep -Seconds 4
 if($proc.HasExited) {throw 'Portable foobar exited before isolated SDK QA'}
 $dispatch=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList @('/add','/immediate',$source) -Wait -PassThru
 if($dispatch.ExitCode -ne 0) {throw 'QA FLAC source not accepted'}
 Start-Sleep -Seconds 2
 $command='/context_command:"Music Metadata Studio/QA write synthetic tags" "'+$source+'"'
 $forward=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList $command -Wait -PassThru
 if($forward.ExitCode -ne 0) {throw 'Physical writer context command failed'}
 # foobar's /context_command permits the leaf name on some host versions.
 # Verify the component was entered before waiting for an SDK mutation.
 for($i=0;$i -lt 20 -and -not (Test-Path $env:MMS_QA_REPORT_PATH);$i++) {
    Start-Sleep -Milliseconds 200
 }
 if(-not (Test-Path $env:MMS_QA_REPORT_PATH)) {
    $leaf='/context_command:"QA write synthetic tags" "'+$source+'"'
    $retry=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList $leaf -Wait -PassThru
    if($retry.ExitCode -ne 0) {throw 'QA leaf context dispatch rejected'}
    for($i=0;$i -lt 20 -and -not (Test-Path $env:MMS_QA_REPORT_PATH);$i++) {
       Start-Sleep -Milliseconds 200
    }
 }
 if(-not (Test-Path $env:MMS_QA_REPORT_PATH)) {
    throw 'QA_NATIVE_CONTEXT_NOT_ENTERED (neither full path nor leaf command)'
 }
 $changed=$false
 for($i=0;$i -lt 75;$i++) {
    if($proc.HasExited) {throw 'foobar exited during SDK write'}
    if((Test-Path $source) -and
       ([IO.File]::ReadAllBytes($source) -join ',') -ne
       ([IO.File]::ReadAllBytes($original) -join ',')) {
       $changed=$true
       break
    }
    Start-Sleep -Milliseconds 400
 }
 if(-not $changed) {
    # The QA component emits ONLY bounded static result codes here.
    $diagnostic=(Get-Content -LiteralPath $env:MMS_QA_REPORT_PATH -Raw -ErrorAction SilentlyContinue)
    throw "SDK_DISPOSABLE_WRITE_NO_CHANGE; component evidence: $diagnostic"
 }
 & python (Join-Path $PSScriptRoot 'assert-physical-writer-qa.py') $original $source 'written'
 if($LASTEXITCODE -ne 0) {throw 'SDK postwrite readback rejected'}
 Copy-Item -LiteralPath $source -Destination $after -ErrorAction Stop
 $time=(Get-Item $source).LastWriteTimeUtc
 # Force a second *deliberate* invocation; the writer must detect exact noop.
 $again=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList $command -Wait -PassThru
 if($again.ExitCode -ne 0) {throw 'Second writer context request failed'}
 Start-Sleep -Seconds 4
 & python (Join-Path $PSScriptRoot 'assert-physical-writer-qa.py') $after $source 'noop'
 if($LASTEXITCODE -ne 0 -or (Get-Item $source).LastWriteTimeUtc -ne $time) {
   throw 'Second identical request wrote a file'
 }
 Write-Host "MMS_NATIVE_PHYSICAL_WRITER_QA_$Architecture=PASS"
} finally {
 Remove-Item Env:MMS_QA_WRITE_SOURCE -ErrorAction SilentlyContinue
 Remove-Item Env:MMS_QA_REPORT_PATH -ErrorAction SilentlyContinue
 if($proc) {
   try {if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force -ErrorAction Stop}}
   catch { Write-Warning 'Could not stop isolated foobar QA process' }
 }
 Remove-Item -LiteralPath $work -Force -Recurse -ErrorAction SilentlyContinue
}
