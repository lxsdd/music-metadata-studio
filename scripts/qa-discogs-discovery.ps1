# Protected Discogs read-only search -> exact-edition acceptance probe.
# SelfTest: deterministic and offline. Live: protected discogs-qa secret only.
[CmdletBinding()]
param(
    [ValidateSet('SelfTest','Live')][string]$Mode='SelfTest',
    [ValidateSet('38012673','37332711','36397609','36819730')]
    [string]$ReleaseId='37332711'
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'

function Assert-QA([bool]$Condition,[string]$Reason) {
    if (-not $Condition) { throw "DISCOGS_QA_FAIL: $Reason" }
}
function Test-ExactSearchIdentity($Payload,[string]$Id) {
    if ($null -eq $Payload -or $null -eq $Payload.pagination -or
        $null -eq $Payload.results -or
        [int]$Payload.pagination.page -ne 1 -or
        [int]$Payload.pagination.per_page -ne 20) {
        throw 'DISCOGS_QA_FAIL: search pagination invalid'
    }
    foreach ($hit in @($Payload.results)) {
        if ($null -ne $hit -and [string]$hit.type -eq 'release' -and
            [string]$hit.id -eq $Id) { return $true }
    }
    return $false
}
function Assert-ExactEdition($Payload,[string]$Id) {
    Assert-QA ($null -ne $Payload -and [string]$Payload.id -eq $Id) 'wrong edition'
    Assert-QA (-not [string]::IsNullOrWhiteSpace([string]$Payload.title)) 'title missing'
    Assert-QA ($null -ne $Payload.tracklist -and
        @($Payload.tracklist).Count -gt 0) 'tracklist missing'
    $named=@($Payload.tracklist | Where-Object {
        $null -ne $_ -and -not [string]::IsNullOrWhiteSpace([string]$_.title)
    })
    Assert-QA ($named.Count -gt 0) 'no named release tracks'
}
function Get-OfficialSearchPaths([string]$Title,[string]$Artist) {
    $t=[uri]::EscapeDataString($Title)
    $a=[uri]::EscapeDataString($Artist)
    # Documented Discogs C++ provider path variants: never a scrape.
    return @(
        "/database/search?track=$t&type=release&artist=$a&page=1&per_page=20"
        "/database/search?track=$t&type=release&page=1&per_page=20"
        "/database/search?release_title=$t&type=release&page=1&per_page=20"
        "/database/search?q=$t&type=release&page=1&per_page=20"
        "/database/search?q=$a&type=release&page=1&per_page=20"
    )
}
function Get-OfficialJson([string]$Path,[string]$Token) {
    Assert-QA ($Path.StartsWith('/releases/') -or
        $Path.StartsWith('/database/search?')) 'unqualified official route'
    $headers=@{
        Authorization="Discogs token=$Token"
        Accept='application/json'
        'User-Agent'='MusicMetadataStudioQA/0.1 (+https://github.com/lxsdd/music-metadata-studio)'
    }
    try {
        return Invoke-RestMethod -Method Get -Uri ("https://api.discogs.com"+$Path) -Headers $headers -TimeoutSec 20 -MaximumRedirection 0
    } catch {
        # Suppress raw status, remote body, headers and the secret.
        throw 'DISCOGS_QA_FAIL: official HTTPS lookup failed (response suppressed)'
    } finally {
        $headers.Clear()
    }
}

if ($Mode -eq 'SelfTest') {
    $search=ConvertFrom-Json '{"pagination":{"page":1,"per_page":20},"results":[{"type":"master","id":37332711},{"type":"release","id":37332711}]}'
    Assert-QA (Test-ExactSearchIdentity $search '37332711') 'exact release fixture not found'
    Assert-QA (-not (Test-ExactSearchIdentity $search '38012673')) 'foreign release accepted'
    $master=ConvertFrom-Json '{"pagination":{"page":1,"per_page":20},"results":[{"type":"master","id":37332711}]}'
    Assert-QA (-not (Test-ExactSearchIdentity $master '37332711')) 'master substituted for edition'
    $bad=ConvertFrom-Json '{"pagination":{"page":2,"per_page":20},"results":[]}'
    $blocked=$false
    try { [void](Test-ExactSearchIdentity $bad '37332711') } catch { $blocked=$true }
    Assert-QA $blocked 'unverified pagination accepted'
    Assert-ExactEdition (ConvertFrom-Json '{"id":37332711,"title":"Synthetic","tracklist":[{"title":"Synthetic track"}]}') '37332711'
    $blocked=$false
    try { Assert-ExactEdition (ConvertFrom-Json '{"id":37332711,"title":"Empty","tracklist":[]}') '37332711' } catch { $blocked=$true }
    Assert-QA $blocked 'empty edition accepted'
    $paths=Get-OfficialSearchPaths '1000 Years' 'Maywave'
    Assert-QA ($paths.Count -eq 5 -and
        $paths[0].Contains('track=1000%20Years') -and
        $paths[0].Contains('artist=Maywave') -and
        $paths[1] -notmatch 'artist=' -and
        $paths[4].Contains('q=Maywave')) 'bounded search contract mismatch'
    Write-Output 'PASS: protected Discogs search-to-edition contract (offline; no secrets)'
    return
}

$cases=@{
    '38012673'=@{Title='Via Balearia';Artist='Estiva'}
    '37332711'=@{Title='1000 Years';Artist='Maywave'}
    '36397609'=@{Title='Only You';Artist='Matt Fax'}
    '36819730'=@{Title='Till You Let Go';Artist='Matt Fax'}
}
$case=$cases[$ReleaseId]
Assert-QA ($null -ne $case) 'unknown public regression'
$token=[string]$env:MMS_DISCOGS_QA_TOKEN
Assert-QA ($token -cmatch '^[A-Za-z0-9_-]{8,256}$') 'protected token unavailable or invalid'
try {
    $edition=Get-OfficialJson "/releases/$ReleaseId" $token
    Assert-ExactEdition $edition $ReleaseId
    Write-Output 'PASS: authorized exact edition and tracklist'
    $found=$false
    $stage=0
    foreach ($path in (Get-OfficialSearchPaths $case.Title $case.Artist)) {
        Start-Sleep -Milliseconds 1700
        $stage++
        $search=Get-OfficialJson $path $token
        if (Test-ExactSearchIdentity $search $ReleaseId) { $found=$true; break }
    }
    Assert-QA $found "exact release not discovered after five bounded first-page strategies (public ID $ReleaseId)"
    Write-Output "PASS: AUTHORIZED_SEARCH -> EXACT_EDITION -> TRACKLIST, stage $stage/5, public ID $ReleaseId"
} finally {
    # PowerShell cannot guarantee secure erasure of immutable strings.
    # No token or payload is written to any log, URL, file or artifact.
    Remove-Variable token -ErrorAction SilentlyContinue
}
