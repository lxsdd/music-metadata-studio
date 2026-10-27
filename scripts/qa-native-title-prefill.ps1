#requires -Version 7.2
[CmdletBinding()]
param(
 [Parameter(Mandatory)][ValidateSet('Win32','x64')][string]$Architecture,
 [Parameter(Mandatory)][string]$Exe,
 [Parameter(Mandatory)][string]$HostDir,
 [Parameter(Mandatory)][string]$Source,
 [Parameter(Mandatory)][uint32]$FoobarPid
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if($env:GITHUB_ACTIONS -cne 'true' -or
   -not (Test-Path -LiteralPath $env:RUNNER_TEMP -PathType Container) -or
   [IO.Path]::GetFileName($Source) -cne 'mms-writer-qa.flac' -or
   -not (Test-Path -LiteralPath $Source -PathType Leaf) -or
   -not (Test-Path -LiteralPath $Exe -PathType Leaf) -or
   $FoobarPid -eq 0) {
  throw 'MMS_NATIVE_TITLE_QA_SOURCE_UNQUALIFIED'
}
$root=[IO.Path]::GetFullPath($env:RUNNER_TEMP).TrimEnd('\','/')
$physical=[IO.Path]::GetFullPath($Source)
if(-not $physical.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -and
   -not $physical.StartsWith($root+'/',[StringComparison]::OrdinalIgnoreCase)) {
  throw 'MMS_NATIVE_TITLE_QA_NOT_RUNNER_TEMP'
}
$before=[IO.File]::ReadAllBytes($physical)
$beforeTime=(Get-Item -LiteralPath $physical).LastWriteTimeUtc

# This is a narrow Studio-owned, disposable native EDIT test. It does not
# reimplement or replace the shared authenticated Discogs host-smoke action.
# GetWindowTextW is permitted only for the top-level window *caption*.
# Reading another process's EDIT requires the OS-marshalled WM_GETTEXT system
# message; otherwise GetWindowTextW silently yields an empty title.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class MmsSyntheticTitleHost {
 [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="FindWindowW")]
 public static extern IntPtr FindWindow(string cls, string caption);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
 [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageTimeoutW")]
 private static extern IntPtr SendText(IntPtr h, uint message, IntPtr cap,
    [Out] StringBuilder buffer, uint flags, uint timeout, out IntPtr result);
 [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageTimeoutW")]
 private static extern IntPtr SendClose(IntPtr h, uint message, IntPtr w,
    IntPtr l, uint flags, uint timeout, out IntPtr result);
 public static string EditText(IntPtr dialog, int id) {
    var edit=GetDlgItem(dialog,id);
    if(edit==IntPtr.Zero) return null;
    var buffer=new StringBuilder(128);
    IntPtr result;
    if(SendText(edit,0x000D,new IntPtr(buffer.Capacity),buffer,
                2,3000,out result)==IntPtr.Zero) return null;
    return buffer.ToString();
 }
 public static bool Close(IntPtr dialog) {
    IntPtr result;
    return SendClose(dialog,0x0010,IntPtr.Zero,IntPtr.Zero,2,3000,out result)
        != IntPtr.Zero;
 }
}
'@

$title='Music Metadata Studio - Prepare Tracks (Read-only)'
$dialog=[IntPtr]::Zero
try {
 foreach($context in @('Music Metadata Studio/Prepare Tracks (Preview)...','Prepare Tracks (Preview)...')) {
    $arg='/context_command:"'+$context+'" "'+$physical+'"'
    $entry=Start-Process -FilePath $Exe -WorkingDirectory $HostDir -ArgumentList $arg -Wait -PassThru
    if($entry.ExitCode -ne 0) {throw 'MMS_NATIVE_TITLE_QA_CONTEXT_COMMAND_FAILED'}
    for($attempt=0;$attempt -lt 28;$attempt++) {
       Start-Sleep -Milliseconds 250
       $candidate=[MmsSyntheticTitleHost]::FindWindow($null,$title)
       if($candidate -eq [IntPtr]::Zero) {continue}
       [uint32]$pid=0
       [void][MmsSyntheticTitleHost]::GetWindowThreadProcessId($candidate,[ref]$pid)
       if($pid -ne $FoobarPid) {throw 'MMS_NATIVE_TITLE_QA_WRONG_PROCESS'}
       $dialog=$candidate
       break
    }
    if($dialog -ne [IntPtr]::Zero) {break}
 }
 if($dialog -eq [IntPtr]::Zero) {throw 'MMS_NATIVE_TITLE_QA_NO_WORKSPACE'}
 $actual=''
 for($attempt=0;$attempt -lt 24;$attempt++) {
    $actual=[MmsSyntheticTitleHost]::EditText($dialog,291)
    if($actual -ceq '1000 Years') {break}
    Start-Sleep -Milliseconds 250
 }
 if($actual -cne '1000 Years') {
    $classification=if($null -eq $actual){'NO_EDIT_RESPONSE'}elseif($actual.Length -eq 0){'EMPTY'}else{'DIFFERENT'}
    # Never report actual metadata text; this QA is synthetic-only.
    throw "MMS_NATIVE_TITLE_QA_MISMATCH_$classification"
 }
 Write-Host "MMS_NATIVE_ORIGINAL_TITLE_PREFILL_$Architecture=PASS (real tagged FLAC, cross-process WM_GETTEXT)"
} finally {
 if($dialog -ne [IntPtr]::Zero) {
    if(-not [MmsSyntheticTitleHost]::Close($dialog)) {
       throw 'MMS_NATIVE_TITLE_QA_WINDOW_CLOSE_FAILED'
    }
 }
 if(([Convert]::ToHexString([IO.File]::ReadAllBytes($physical))) -cne
       ([Convert]::ToHexString($before)) -or
    (Get-Item -LiteralPath $physical).LastWriteTimeUtc -ne $beforeTime) {
    throw 'MMS_NATIVE_TITLE_QA_MODIFIED_SYNTHETIC_MEDIA'
 }
}
