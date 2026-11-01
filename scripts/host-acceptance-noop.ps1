#requires -Version 5.1
<#
  FIRST real-foobar, PREVIEW-ONLY host acceptance.
  Uses disposable COPIES of one user-selected MP3. Never edits the original.
  Verify compares exact audio/CUE SHA-256 and file creation/modified times.
  The script itself does not load foobar or claim an in-host UI PASS.
#>
[CmdletBinding()]
param(
    [ValidateSet('Prepare', 'Verify')][string]$Mode = 'Prepare',
    [string]$SampleFile,
    [string]$TestFolder
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Inspect-MediaFile {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)
    $item = Get-Item -LiteralPath $LiteralPath -ErrorAction Stop
    if ($item.PSIsContainer) { throw "Expected a file, not a directory: $LiteralPath" }
    return [pscustomobject]@{
        Name = $item.Name
        Length = [long]$item.Length
        Sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        CreatedUtcTicks = [long]$item.CreationTimeUtc.Ticks
        ModifiedUtcTicks = [long]$item.LastWriteTimeUtc.Ticks
    }
}

if ($Mode -eq 'Prepare') {
    if ([string]::IsNullOrWhiteSpace($SampleFile)) {
        throw 'Prepare requires -SampleFile pointing at one ordinary MP3 from your collection.'
    }
    $source = Get-Item -LiteralPath $SampleFile -ErrorAction Stop
    if ($source.PSIsContainer -or $source.Extension -ine '.mp3') {
        throw 'Select one real MP3 file (not a folder, FLAC or virtual CUE subsong).'
    }
    if ([string]::IsNullOrWhiteSpace($TestFolder)) {
        $TestFolder = Join-Path $env:TEMP ('DJMetaHostGate-' + [guid]::NewGuid().ToString('N').Substring(0, 12))
    }
    $TestFolder = [IO.Path]::GetFullPath($TestFolder)
    if (Test-Path -LiteralPath $TestFolder) {
        throw "Refusing to overwrite an existing test directory: $TestFolder"
    }
    New-Item -ItemType Directory -Path $TestFolder -ErrorAction Stop | Out-Null
    $a = Join-Path $TestFolder 'sample-a.mp3'
    $b = Join-Path $TestFolder 'sample-b.mp3'
    # Only Copy-Item reads the user's original; all subsequent operations
    # point at these disposable test copies. No tag writer is ever invoked.
    Copy-Item -LiteralPath $source.FullName -Destination $a -ErrorAction Stop
    Copy-Item -LiteralPath $source.FullName -Destination $b -ErrorAction Stop

    $cue = Join-Path $TestFolder 'sample-a.cue'
    @(
        'REM Music Metadata Studio read-only test fixture',
        'FILE "sample-a.mp3" MP3',
        '  TRACK 01 AUDIO',
        '    INDEX 01 00:00:00'
    ) | Set-Content -LiteralPath $cue -Encoding Ascii

    $record = [pscustomobject]@{
        Contract = 'djmeta-readonly-host-gate-v1'
        CapturedAtUtc = [DateTime]::UtcNow.ToString('o')
        Files = @(
            (Inspect-MediaFile -LiteralPath $a),
            (Inspect-MediaFile -LiteralPath $b),
            (Inspect-MediaFile -LiteralPath $cue)
        )
    }
    $manifest = Join-Path $TestFolder 'BASELINE.json'
    $record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifest -Encoding UTF8
    Write-Host 'PREPARE=PASS; original MP3 not modified.'
    Write-Host "TEST_FOLDER=$TestFolder"
    Write-Host 'Next: add sample-a.mp3, sample-b.mp3 and sample-a.cue to foobar.'
    Write-Host 'Open the Music Metadata Studio Prepare Tracks preview, inspect File locations'
    Write-Host 'and the read-only target context menu; close without applying any writes.'
    Write-Host ('Afterward run: powershell -NoProfile -ExecutionPolicy Bypass -File "' +
        $MyInvocation.MyCommand.Path + '" -Mode Verify -TestFolder "' + $TestFolder + '"')
    return
}

if ([string]::IsNullOrWhiteSpace($TestFolder)) {
    throw 'Verify requires -TestFolder from the Prepare output.'
}
$TestFolder = [IO.Path]::GetFullPath($TestFolder)
$manifest = Join-Path $TestFolder 'BASELINE.json'
$baseline = Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
if ($baseline.Contract -ne 'djmeta-readonly-host-gate-v1') {
    throw 'Baseline format is unsupported; no verification performed.'
}
$expectedNames = @('sample-a.mp3', 'sample-b.mp3', 'sample-a.cue')
$entries = @($baseline.Files)
if ($entries.Count -ne $expectedNames.Count) {
    throw 'Invalid baseline: wrong number of watched media files.'
}
$failures = New-Object System.Collections.Generic.List[string]
foreach ($name in $expectedNames) {
    $matches = @($entries | Where-Object { $_.Name -ceq $name })
    if ($matches.Count -ne 1) {
        $failures.Add("Missing or ambiguous baseline record: $name")
        continue
    }
    $expected = $matches[0]
    $file = Join-Path $TestFolder $name
    try {
        $actual = Inspect-MediaFile -LiteralPath $file
        if ($actual.Length -ne [long]$expected.Length) { $failures.Add("$name size changed") }
        if ($actual.Sha256 -cne [string]$expected.Sha256) { $failures.Add("$name content changed") }
        if ($actual.CreatedUtcTicks -ne [long]$expected.CreatedUtcTicks) {
            $failures.Add("$name creation time changed")
        }
        if ($actual.ModifiedUtcTicks -ne [long]$expected.ModifiedUtcTicks) {
            $failures.Add("$name modification time changed")
        }
    } catch {
        $failures.Add("$name missing or unreadable")
    }
}
$reportPath = Join-Path $TestFolder 'HOST-RESULT.txt'
if ($failures.Count -ne 0) {
    $lines = @('FILE_INTEGRITY=FAIL', 'Host preview or another process modified a disposable test sample.') +
        @($failures)
    $lines | Set-Content -LiteralPath $reportPath -Encoding UTF8
    $lines | ForEach-Object { Write-Host $_ }
    throw "File integrity verification failed; see $reportPath"
}
@(
    'FILE_INTEGRITY=PASS'
    'SHA256=UNCHANGED'
    'CREATION_TIMES=UNCHANGED'
    'MODIFICATION_TIMES=UNCHANGED'
    'HOST_UI=MANUAL_CHECK_REQUIRED'
    'WRITER_BEHAVIOR=NOT_TESTED_NO_WRITER_PRESENT'
) | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host 'FILE_INTEGRITY=PASS; hashes and creation/modified timestamps unchanged.'
Write-Host 'HOST_UI=MANUAL_CHECK_REQUIRED (confirm that the actual foobar dialog worked).'
Write-Host "REPORT=$reportPath"
