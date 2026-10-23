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
$cueSource=Join-Path $work 'mms-writer-qa.cue'
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
 $cueText=[string]::Join([Environment]::NewLine,@(
   'TITLE "Local album"'
   'PERFORMER "DJ"'
   'FILE "mms-writer-qa.flac" WAVE'
   'TRACK 01 AUDIO'
   'TITLE "Sunrise (Extended Mix)"'
   'PERFORMER "DJ"'
   'ISRC GBABC2600001'
   'INDEX 01 00:00:00'
 )) + [Environment]::NewLine
 [IO.File]::WriteAllText($cueSource,$cueText,[Text.UTF8Encoding]::new($false))
 $env:MMS_QA_CUE_SOURCE=$cueSource

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
 $loaded=@((Get-Process -Id $proc.Id -ErrorAction Stop).Modules |
   Where-Object { $_.ModuleName -ieq 'foo_music_metadata_studio.dll' }).Count -eq 1
 if(-not $loaded) { throw 'QA DLL was not loaded into the disposable foobar process' }
 if(-not (Test-Path $env:MMS_QA_REPORT_PATH)) {
   throw 'QA DLL loaded but its init callback did not create runner diagnostics'
 }
 Write-Host 'MMS_QA_COMPONENT_LOAD=PASS (isolated disposable foobar)'
 $dispatch=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList @('/add','/immediate',$source) -Wait -PassThru
 if($dispatch.ExitCode -ne 0) {throw 'QA FLAC source not accepted'}
 Start-Sleep -Seconds 2
 $command='/context_command:"Music Metadata Studio/QA write synthetic tags" "'+$source+'"'
 $forward=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList $command -Wait -PassThru
 if($forward.ExitCode -ne 0) {throw 'Physical writer context command failed'}
 # CLI menu names may be resolved as qualified group paths or leaf names.
 # The on_init trace exists before dispatch, so presence of the report file
 # CANNOT be used as evidence that a context command entered the QA callback.
 $entered=$false
 for($i=0;$i -lt 20;$i++) {
   $diagnostic=(Get-Content -LiteralPath $env:MMS_QA_REPORT_PATH -Raw -ErrorAction SilentlyContinue)
   if($diagnostic -match 'QA_ACTION_ENTERED') {$entered=$true;break}
   Start-Sleep -Milliseconds 200
 }
 if(-not $entered) {
   $leaf='/context_command:"QA write synthetic tags" "'+$source+'"'
   $retry=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList $leaf -Wait -PassThru
   if($retry.ExitCode -ne 0) {throw 'QA leaf context dispatch rejected'}
   for($i=0;$i -lt 20;$i++) {
     $diagnostic=(Get-Content -LiteralPath $env:MMS_QA_REPORT_PATH -Raw -ErrorAction SilentlyContinue)
     if($diagnostic -match 'QA_ACTION_ENTERED') {$entered=$true;break}
     Start-Sleep -Milliseconds 200
   }
 }
 if(-not $entered) {
   throw "QA_NATIVE_CONTEXT_NOT_ENTERED (full/leaf); diagnostics: $diagnostic"
 }
 $changed=$false
 $baseline=[IO.File]::ReadAllBytes($original)
 for($i=0;$i -lt 75;$i++) {
   if($proc.HasExited) {throw 'foobar exited during SDK write'}
   # Foobar's native writer may temporarily hold the file exclusively.
   # Temporary IOException is expected; boundedly wait for a stable read.
   if(Test-Path -LiteralPath $source) {
     try {
       $now=[IO.File]::ReadAllBytes($source)
       if(($now -join ',') -ne ($baseline -join ',')) {
         $changed=$true
         break
       }
     } catch [System.IO.IOException] {
       # Exclusive SDK writer lock; retry, never mark as a changed file.
     }
   }
   Start-Sleep -Milliseconds 400
 }
 if($changed) {
   $readable=$false
   for($i=0;$i -lt 30;$i++) {
     try {
       $probe=[IO.File]::Open($source,[IO.FileMode]::Open,
                             [IO.FileAccess]::Read,[IO.FileShare]::Read)
       $probe.Dispose()
       $readable=$true
       break
     } catch [System.IO.IOException] { Start-Sleep -Milliseconds 300 }
   }
   if(-not $readable) {throw 'Native SDK file lock did not release'}
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
 # The external-CUE controller runs inside the same disposable foobar.
 $audioBeforeCue=[IO.File]::ReadAllBytes($source)
 $cueDispatch=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList @('/add','/immediate',$cueSource) -Wait -PassThru
 if($cueDispatch.ExitCode -ne 0) {throw 'Synthetic CUE source not accepted'}
 Start-Sleep -Seconds 2
 $cueCommand='/context_command:"Music Metadata Studio/QA write synthetic CUE" "'+$cueSource+'"'
 $cueAction=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList $cueCommand -Wait -PassThru
 if($cueAction.ExitCode -ne 0) {throw 'Synthetic CUE context command failed'}
 $cueEntered=$false
 for($i=0;$i -lt 25;$i++) {
   $diagnostic=(Get-Content -LiteralPath $env:MMS_QA_REPORT_PATH -Raw -ErrorAction SilentlyContinue)
   if($diagnostic -match 'QA_CUE_ACTION_ENTERED') {$cueEntered=$true;break}
   Start-Sleep -Milliseconds 200
 }
 if(-not $cueEntered) {
   $cueLeaf='/context_command:"QA write synthetic CUE" "'+$cueSource+'"'
   $retry=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList $cueLeaf -Wait -PassThru
   if($retry.ExitCode -ne 0) {throw 'Synthetic CUE leaf command failed'}
   for($i=0;$i -lt 25;$i++) {
     $diagnostic=(Get-Content -LiteralPath $env:MMS_QA_REPORT_PATH -Raw -ErrorAction SilentlyContinue)
     if($diagnostic -match 'QA_CUE_ACTION_ENTERED') {$cueEntered=$true;break}
     Start-Sleep -Milliseconds 200
   }
 }
 if(-not $cueEntered) {throw "CUE_NATIVE_CONTEXT_NOT_ENTERED: $diagnostic"}
 $cueCommitted=$false
 for($i=0;$i -lt 40;$i++) {
   $diagnostic=(Get-Content -LiteralPath $env:MMS_QA_REPORT_PATH -Raw -ErrorAction SilentlyContinue)
   if($diagnostic -match 'QA_CUE_COMMITTED') {$cueCommitted=$true;break}
   if($diagnostic -match 'QA_CUE_EXCEPTION|QA_CUE_COMMIT_REFUSED|QA_CUE_SOURCE_REJECTED') {
     throw "CUE_NATIVE_HOST_TRANSACTION_REFUSED: $diagnostic"
   }
   Start-Sleep -Milliseconds 200
 }
 if(-not $cueCommitted) {throw "CUE_NATIVE_HOST_NO_COMMIT: $diagnostic"}
 $expectedCue=$cueText.Replace('TITLE "Local album"','TITLE "Other album"').Replace(
   'TITLE "Sunrise (Extended Mix)"','TITLE "Corrected Sunrise (Extended Mix)"')
 $expectedCueBytes=[Text.UTF8Encoding]::new($false).GetBytes($expectedCue)
 $actualCueBytes=[IO.File]::ReadAllBytes($cueSource)
 if(($expectedCueBytes -join ',') -cne ($actualCueBytes -join ',')) {
   throw 'Native CUE postimage differs from exact two-value projection'
 }
 if(($audioBeforeCue -join ',') -cne ([IO.File]::ReadAllBytes($source) -join ',')) {
   throw 'CUE-only write unexpectedly modified audio bytes'
 }
 if(@(Get-ChildItem -LiteralPath $work -File -Filter '*.bak').Count -ne 0) {
   throw 'Verified CUE transaction left an unreported backup'
 }
 $cueTime=(Get-Item $cueSource).LastWriteTimeUtc
 $cueAgain=Start-Process -FilePath $exe -WorkingDirectory $hostDir -ArgumentList $cueCommand -Wait -PassThru
 if($cueAgain.ExitCode -ne 0) {throw 'CUE second context request failed'}
 $cueNoop=$false
 for($i=0;$i -lt 40;$i++) {
   $diagnostic=(Get-Content -LiteralPath $env:MMS_QA_REPORT_PATH -Raw -ErrorAction SilentlyContinue)
   if($diagnostic -match 'QA_CUE_NOOP') {$cueNoop=$true;break}
   Start-Sleep -Milliseconds 200
 }
 if(-not $cueNoop -or
    ($expectedCueBytes -join ',') -cne ([IO.File]::ReadAllBytes($cueSource) -join ',') -or
    (Get-Item $cueSource).LastWriteTimeUtc -ne $cueTime) {
   throw "CUE second-pass no-op failed: $diagnostic"
 }
 Write-Host "MMS_NATIVE_EXTERNAL_CUE_HOST_QA_$Architecture=PASS"
 Write-Host "MMS_NATIVE_PHYSICAL_WRITER_QA_$Architecture=PASS"
} finally {
 Remove-Item Env:MMS_QA_WRITE_SOURCE -ErrorAction SilentlyContinue
 Remove-Item Env:MMS_QA_CUE_SOURCE -ErrorAction SilentlyContinue
 Remove-Item Env:MMS_QA_REPORT_PATH -ErrorAction SilentlyContinue
 if($proc) {
   try {if(-not $proc.HasExited){Stop-Process -Id $proc.Id -Force -ErrorAction Stop}}
   catch { Write-Warning 'Could not stop isolated foobar QA process' }
 }
 Remove-Item -LiteralPath $work -Force -Recurse -ErrorAction SilentlyContinue
}
