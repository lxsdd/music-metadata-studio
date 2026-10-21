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
# A linked master year never borrows the year of an exact edition.
function Test-LinkedMasterYear($Payload,[string]$ExpectedId) {
    Assert-QA ($null -ne $Payload -and [string]$Payload.id -ceq $ExpectedId) 'linked master ID mismatch'
    $year=''
    if ($null -ne $Payload -and
        $null -ne $Payload.PSObject.Properties['year']) {
        $year=[string]$Payload.year
    }
    if ($year -notmatch '^[0-9]{4}
    if ([string]::IsNullOrWhiteSpace($Core)) { $Core=$Title }
    $t=[uri]::EscapeDataString($Title)
    $c=[uri]::EscapeDataString($Core)
    $a=[uri]::EscapeDataString($Artist)
    # Mirrors the native C++ track_search_plan on these qualified fixtures.
    # The FIRST requests always contain the original complete ARTIST.
    $paths=[System.Collections.Generic.List[string]]::new()
    $paths.Add("/database/search?q=$t&type=release&artist=$a&page=1&per_page=20")
    $paths.Add("/database/search?track=$t&type=release&artist=$a&page=1&per_page=20")
    # Only a deliberately joined X collaboration is broadened here;
    # the ampersand in "Kyau & Albert" is part of the same artist name.
    if ($Artist -cmatch '^(.+?) [xX] ([^\r\n]+)$') {
        $guest=[uri]::EscapeDataString($Matches[2])
        $paths.Add("/database/search?track=$c&type=release&artist=$guest&page=1&per_page=20")
        $paths.Add("/database/search?q=$c&type=release&artist=$guest&page=1&per_page=20")
    }
    $paths.Add("/database/search?track=$t&type=release&page=1&per_page=20")
    if ($Core -cne $Title) {
        $paths.Add("/database/search?track=$c&type=release&page=1&per_page=20")
    }
    $paths.Add("/database/search?q=$c&type=release&page=1&per_page=20")
    $paths.Add("/database/search?artist=$a&type=release&page=1&per_page=20")
    Assert-QA ($paths.Count -gt 0 -and $paths.Count -le 16) 'unbounded Discogs stages'
    return @($paths.ToArray())
}
function Get-OfficialJson([string]$Path,[string]$Token) {
    Assert-QA ($Path.StartsWith('/releases/') -or
        $Path.StartsWith('/database/search?') -or
        ($Path -cmatch '^/masters/[1-9][0-9]{0,11}
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
    $master_fixture=ConvertFrom-Json '{"id":4343,"year":1998,"main_release":37332711}'
    Assert-QA ((Test-LinkedMasterYear $master_fixture '4343') -eq '1998') 'linked master year fixture failed'
    Assert-QA ((Test-LinkedMasterYear (ConvertFrom-Json '{"id":4343,"year":0}') '4343') -eq '') 'unknown master year inferred from edition'
    Assert-QA ((Test-LinkedMasterYear (ConvertFrom-Json '{"id":4343}') '4343') -eq '') 'missing master year fabricated'
    $bad_master=$false
    try { [void](Test-LinkedMasterYear $master_fixture '4344') }
    catch { $bad_master=$true }
    Assert-QA $bad_master 'foreign master detail accepted'
    $blocked=$false
    try { Assert-ExactEdition (ConvertFrom-Json '{"id":37332711,"title":"Empty","tracklist":[]}') '37332711' } catch { $blocked=$true }
    Assert-QA $blocked 'empty edition accepted'
    $paths=Get-OfficialSearchPaths '1000 Years (DJ Version)' 'Kyau & Albert X Maywave' '1000 Years'
    Assert-QA ($paths.Count -eq 8 -and
        $paths[0] -eq '/database/search?q=1000%20Years%20%28DJ%20Version%29&type=release&artist=Kyau%20%26%20Albert%20X%20Maywave&page=1&per_page=20' -and
        $paths[1].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
        $paths[2] -eq '/database/search?track=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[3] -eq '/database/search?q=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[4] -notmatch 'artist=' -and
        $paths[7].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave')) 'full original artist / collaborator discovery plan unqualified'
    $simple=Get-OfficialSearchPaths 'Only You' 'Matt Fax'
    Assert-QA ($simple.Count -eq 5 -and
        $simple[0].Contains('artist=Matt%20Fax') -and
        $simple[2] -notmatch 'artist=') 'normal solo-artist discovery changed'
    Write-Output 'PASS: protected Discogs search-to-edition contract (offline; no secrets)'
    return
}

$cases=@{
    '38012673'=@{Title='Via Balearia';Artist='Estiva'}
    '37332711'=@{Title='1000 Years (DJ Version)';Core='1000 Years';Artist='Kyau & Albert X Maywave'}
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
    $masterIdText=''
    if ($null -ne $edition.PSObject.Properties['master_id']) {
        $masterIdText=[string]$edition.master_id
    }
    if (-not [string]::IsNullOrWhiteSpace($masterIdText)) {
        Assert-QA ($masterIdText -cmatch '^[1-9][0-9]{0,11}
    $stage=0
    $core=if ($case.ContainsKey('Core')) { [string]$case.Core } else { [string]$case.Title }
    $paths=@(Get-OfficialSearchPaths $case.Title $case.Artist $core)
    if ($ReleaseId -eq '37332711') {
        Assert-QA ($paths.Count -eq 8 -and
            $paths[0].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
            $paths[2].Contains('artist=Maywave')) 'original complete artist query was bypassed'
    }
    foreach ($path in $paths) {
        Start-Sleep -Milliseconds 1700
        $stage++
        $search=Get-OfficialJson $path $token
        if (Test-ExactSearchIdentity $search $ReleaseId) { $found=$true; break }
        # Production auto-continues after empty hits, otherwise the user
        # explicitly clicks Search to advance from an irrelevant result.
    }
    Assert-QA $found "exact edition not discovered from full original ARTIST through $($paths.Count) bounded first-page strategies (public ID $ReleaseId)"
    Write-Output "PASS: FULL_ORIGINAL_ARTIST -> AUTHORIZED_SEARCH -> EXACT_EDITION -> TRACKLIST, stage $stage/$($paths.Count), public ID $ReleaseId"
} finally {
    # PowerShell cannot guarantee secure erasure of immutable strings.
    # No token or payload is written to any log, URL, file or artifact.
    Remove-Variable token -ErrorAction SilentlyContinue
}
) { return '' }
    $number=[int]$year
    if ($number -lt 1850 -or $number -gt 2200) { return '' }
    return $year
}

function Get-OfficialSearchPaths([string]$Title,[string]$Artist,[string]$Core='') {
    if ([string]::IsNullOrWhiteSpace($Core)) { $Core=$Title }
    $t=[uri]::EscapeDataString($Title)
    $c=[uri]::EscapeDataString($Core)
    $a=[uri]::EscapeDataString($Artist)
    # Mirrors the native C++ track_search_plan on these qualified fixtures.
    # The FIRST requests always contain the original complete ARTIST.
    $paths=[System.Collections.Generic.List[string]]::new()
    $paths.Add("/database/search?q=$t&type=release&artist=$a&page=1&per_page=20")
    $paths.Add("/database/search?track=$t&type=release&artist=$a&page=1&per_page=20")
    # Only a deliberately joined X collaboration is broadened here;
    # the ampersand in "Kyau & Albert" is part of the same artist name.
    if ($Artist -cmatch '^(.+?) [xX] ([^\r\n]+)$') {
        $guest=[uri]::EscapeDataString($Matches[2])
        $paths.Add("/database/search?track=$c&type=release&artist=$guest&page=1&per_page=20")
        $paths.Add("/database/search?q=$c&type=release&artist=$guest&page=1&per_page=20")
    }
    $paths.Add("/database/search?track=$t&type=release&page=1&per_page=20")
    if ($Core -cne $Title) {
        $paths.Add("/database/search?track=$c&type=release&page=1&per_page=20")
    }
    $paths.Add("/database/search?q=$c&type=release&page=1&per_page=20")
    $paths.Add("/database/search?artist=$a&type=release&page=1&per_page=20")
    Assert-QA ($paths.Count -gt 0 -and $paths.Count -le 16) 'unbounded Discogs stages'
    return @($paths.ToArray())
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
    $paths=Get-OfficialSearchPaths '1000 Years (DJ Version)' 'Kyau & Albert X Maywave' '1000 Years'
    Assert-QA ($paths.Count -eq 8 -and
        $paths[0] -eq '/database/search?q=1000%20Years%20%28DJ%20Version%29&type=release&artist=Kyau%20%26%20Albert%20X%20Maywave&page=1&per_page=20' -and
        $paths[1].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
        $paths[2] -eq '/database/search?track=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[3] -eq '/database/search?q=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[4] -notmatch 'artist=' -and
        $paths[7].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave')) 'full original artist / collaborator discovery plan unqualified'
    $simple=Get-OfficialSearchPaths 'Only You' 'Matt Fax'
    Assert-QA ($simple.Count -eq 5 -and
        $simple[0].Contains('artist=Matt%20Fax') -and
        $simple[2] -notmatch 'artist=') 'normal solo-artist discovery changed'
    Write-Output 'PASS: protected Discogs search-to-edition contract (offline; no secrets)'
    return
}

$cases=@{
    '38012673'=@{Title='Via Balearia';Artist='Estiva'}
    '37332711'=@{Title='1000 Years (DJ Version)';Core='1000 Years';Artist='Kyau & Albert X Maywave'}
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
    $core=if ($case.ContainsKey('Core')) { [string]$case.Core } else { [string]$case.Title }
    $paths=@(Get-OfficialSearchPaths $case.Title $case.Artist $core)
    if ($ReleaseId -eq '37332711') {
        Assert-QA ($paths.Count -eq 8 -and
            $paths[0].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
            $paths[2].Contains('artist=Maywave')) 'original complete artist query was bypassed'
    }
    foreach ($path in $paths) {
        Start-Sleep -Milliseconds 1700
        $stage++
        $search=Get-OfficialJson $path $token
        if (Test-ExactSearchIdentity $search $ReleaseId) { $found=$true; break }
        # Production auto-continues after empty hits, otherwise the user
        # explicitly clicks Search to advance from an irrelevant result.
    }
    Assert-QA $found "exact edition not discovered from full original ARTIST through $($paths.Count) bounded first-page strategies (public ID $ReleaseId)"
    Write-Output "PASS: FULL_ORIGINAL_ARTIST -> AUTHORIZED_SEARCH -> EXACT_EDITION -> TRACKLIST, stage $stage/$($paths.Count), public ID $ReleaseId"
} finally {
    # PowerShell cannot guarantee secure erasure of immutable strings.
    # No token or payload is written to any log, URL, file or artifact.
    Remove-Variable token -ErrorAction SilentlyContinue
}
)) 'unqualified official route'
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
    $paths=Get-OfficialSearchPaths '1000 Years (DJ Version)' 'Kyau & Albert X Maywave' '1000 Years'
    Assert-QA ($paths.Count -eq 8 -and
        $paths[0] -eq '/database/search?q=1000%20Years%20%28DJ%20Version%29&type=release&artist=Kyau%20%26%20Albert%20X%20Maywave&page=1&per_page=20' -and
        $paths[1].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
        $paths[2] -eq '/database/search?track=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[3] -eq '/database/search?q=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[4] -notmatch 'artist=' -and
        $paths[7].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave')) 'full original artist / collaborator discovery plan unqualified'
    $simple=Get-OfficialSearchPaths 'Only You' 'Matt Fax'
    Assert-QA ($simple.Count -eq 5 -and
        $simple[0].Contains('artist=Matt%20Fax') -and
        $simple[2] -notmatch 'artist=') 'normal solo-artist discovery changed'
    Write-Output 'PASS: protected Discogs search-to-edition contract (offline; no secrets)'
    return
}

$cases=@{
    '38012673'=@{Title='Via Balearia';Artist='Estiva'}
    '37332711'=@{Title='1000 Years (DJ Version)';Core='1000 Years';Artist='Kyau & Albert X Maywave'}
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
    $core=if ($case.ContainsKey('Core')) { [string]$case.Core } else { [string]$case.Title }
    $paths=@(Get-OfficialSearchPaths $case.Title $case.Artist $core)
    if ($ReleaseId -eq '37332711') {
        Assert-QA ($paths.Count -eq 8 -and
            $paths[0].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
            $paths[2].Contains('artist=Maywave')) 'original complete artist query was bypassed'
    }
    foreach ($path in $paths) {
        Start-Sleep -Milliseconds 1700
        $stage++
        $search=Get-OfficialJson $path $token
        if (Test-ExactSearchIdentity $search $ReleaseId) { $found=$true; break }
        # Production auto-continues after empty hits, otherwise the user
        # explicitly clicks Search to advance from an irrelevant result.
    }
    Assert-QA $found "exact edition not discovered from full original ARTIST through $($paths.Count) bounded first-page strategies (public ID $ReleaseId)"
    Write-Output "PASS: FULL_ORIGINAL_ARTIST -> AUTHORIZED_SEARCH -> EXACT_EDITION -> TRACKLIST, stage $stage/$($paths.Count), public ID $ReleaseId"
} finally {
    # PowerShell cannot guarantee secure erasure of immutable strings.
    # No token or payload is written to any log, URL, file or artifact.
    Remove-Variable token -ErrorAction SilentlyContinue
}
) { return '' }
    $number=[int]$year
    if ($number -lt 1850 -or $number -gt 2200) { return '' }
    return $year
}

function Get-OfficialSearchPaths([string]$Title,[string]$Artist,[string]$Core='') {
    if ([string]::IsNullOrWhiteSpace($Core)) { $Core=$Title }
    $t=[uri]::EscapeDataString($Title)
    $c=[uri]::EscapeDataString($Core)
    $a=[uri]::EscapeDataString($Artist)
    # Mirrors the native C++ track_search_plan on these qualified fixtures.
    # The FIRST requests always contain the original complete ARTIST.
    $paths=[System.Collections.Generic.List[string]]::new()
    $paths.Add("/database/search?q=$t&type=release&artist=$a&page=1&per_page=20")
    $paths.Add("/database/search?track=$t&type=release&artist=$a&page=1&per_page=20")
    # Only a deliberately joined X collaboration is broadened here;
    # the ampersand in "Kyau & Albert" is part of the same artist name.
    if ($Artist -cmatch '^(.+?) [xX] ([^\r\n]+)$') {
        $guest=[uri]::EscapeDataString($Matches[2])
        $paths.Add("/database/search?track=$c&type=release&artist=$guest&page=1&per_page=20")
        $paths.Add("/database/search?q=$c&type=release&artist=$guest&page=1&per_page=20")
    }
    $paths.Add("/database/search?track=$t&type=release&page=1&per_page=20")
    if ($Core -cne $Title) {
        $paths.Add("/database/search?track=$c&type=release&page=1&per_page=20")
    }
    $paths.Add("/database/search?q=$c&type=release&page=1&per_page=20")
    $paths.Add("/database/search?artist=$a&type=release&page=1&per_page=20")
    Assert-QA ($paths.Count -gt 0 -and $paths.Count -le 16) 'unbounded Discogs stages'
    return @($paths.ToArray())
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
    $paths=Get-OfficialSearchPaths '1000 Years (DJ Version)' 'Kyau & Albert X Maywave' '1000 Years'
    Assert-QA ($paths.Count -eq 8 -and
        $paths[0] -eq '/database/search?q=1000%20Years%20%28DJ%20Version%29&type=release&artist=Kyau%20%26%20Albert%20X%20Maywave&page=1&per_page=20' -and
        $paths[1].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
        $paths[2] -eq '/database/search?track=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[3] -eq '/database/search?q=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[4] -notmatch 'artist=' -and
        $paths[7].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave')) 'full original artist / collaborator discovery plan unqualified'
    $simple=Get-OfficialSearchPaths 'Only You' 'Matt Fax'
    Assert-QA ($simple.Count -eq 5 -and
        $simple[0].Contains('artist=Matt%20Fax') -and
        $simple[2] -notmatch 'artist=') 'normal solo-artist discovery changed'
    Write-Output 'PASS: protected Discogs search-to-edition contract (offline; no secrets)'
    return
}

$cases=@{
    '38012673'=@{Title='Via Balearia';Artist='Estiva'}
    '37332711'=@{Title='1000 Years (DJ Version)';Core='1000 Years';Artist='Kyau & Albert X Maywave'}
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
    $core=if ($case.ContainsKey('Core')) { [string]$case.Core } else { [string]$case.Title }
    $paths=@(Get-OfficialSearchPaths $case.Title $case.Artist $core)
    if ($ReleaseId -eq '37332711') {
        Assert-QA ($paths.Count -eq 8 -and
            $paths[0].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
            $paths[2].Contains('artist=Maywave')) 'original complete artist query was bypassed'
    }
    foreach ($path in $paths) {
        Start-Sleep -Milliseconds 1700
        $stage++
        $search=Get-OfficialJson $path $token
        if (Test-ExactSearchIdentity $search $ReleaseId) { $found=$true; break }
        # Production auto-continues after empty hits, otherwise the user
        # explicitly clicks Search to advance from an irrelevant result.
    }
    Assert-QA $found "exact edition not discovered from full original ARTIST through $($paths.Count) bounded first-page strategies (public ID $ReleaseId)"
    Write-Output "PASS: FULL_ORIGINAL_ARTIST -> AUTHORIZED_SEARCH -> EXACT_EDITION -> TRACKLIST, stage $stage/$($paths.Count), public ID $ReleaseId"
} finally {
    # PowerShell cannot guarantee secure erasure of immutable strings.
    # No token or payload is written to any log, URL, file or artifact.
    Remove-Variable token -ErrorAction SilentlyContinue
}
) 'invalid master linkage'
        $master=Get-OfficialJson "/masters/$masterIdText" $token
        $masterYear=Test-LinkedMasterYear $master $masterIdText
        if ([string]::IsNullOrWhiteSpace($masterYear)) {
            Write-Output 'PASS: LINKED_MASTER_ID_VERIFIED, MASTER_YEAR_UNAVAILABLE; no inferred date'
        } else {
            Write-Output 'PASS: LINKED_MASTER_ID_VERIFIED, MASTER_YEAR_QUALIFIED, EDITION_DATE_SEPARATE'
        }
    } else {
        Write-Output 'PASS: EXACT_EDITION_HAS_NO_MASTER_LINK; no invented master date'
    }
    $found=$false
    $stage=0
    $core=if ($case.ContainsKey('Core')) { [string]$case.Core } else { [string]$case.Title }
    $paths=@(Get-OfficialSearchPaths $case.Title $case.Artist $core)
    if ($ReleaseId -eq '37332711') {
        Assert-QA ($paths.Count -eq 8 -and
            $paths[0].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
            $paths[2].Contains('artist=Maywave')) 'original complete artist query was bypassed'
    }
    foreach ($path in $paths) {
        Start-Sleep -Milliseconds 1700
        $stage++
        $search=Get-OfficialJson $path $token
        if (Test-ExactSearchIdentity $search $ReleaseId) { $found=$true; break }
        # Production auto-continues after empty hits, otherwise the user
        # explicitly clicks Search to advance from an irrelevant result.
    }
    Assert-QA $found "exact edition not discovered from full original ARTIST through $($paths.Count) bounded first-page strategies (public ID $ReleaseId)"
    Write-Output "PASS: FULL_ORIGINAL_ARTIST -> AUTHORIZED_SEARCH -> EXACT_EDITION -> TRACKLIST, stage $stage/$($paths.Count), public ID $ReleaseId"
} finally {
    # PowerShell cannot guarantee secure erasure of immutable strings.
    # No token or payload is written to any log, URL, file or artifact.
    Remove-Variable token -ErrorAction SilentlyContinue
}
) { return '' }
    $number=[int]$year
    if ($number -lt 1850 -or $number -gt 2200) { return '' }
    return $year
}

function Get-OfficialSearchPaths([string]$Title,[string]$Artist,[string]$Core='') {
    if ([string]::IsNullOrWhiteSpace($Core)) { $Core=$Title }
    $t=[uri]::EscapeDataString($Title)
    $c=[uri]::EscapeDataString($Core)
    $a=[uri]::EscapeDataString($Artist)
    # Mirrors the native C++ track_search_plan on these qualified fixtures.
    # The FIRST requests always contain the original complete ARTIST.
    $paths=[System.Collections.Generic.List[string]]::new()
    $paths.Add("/database/search?q=$t&type=release&artist=$a&page=1&per_page=20")
    $paths.Add("/database/search?track=$t&type=release&artist=$a&page=1&per_page=20")
    # Only a deliberately joined X collaboration is broadened here;
    # the ampersand in "Kyau & Albert" is part of the same artist name.
    if ($Artist -cmatch '^(.+?) [xX] ([^\r\n]+)$') {
        $guest=[uri]::EscapeDataString($Matches[2])
        $paths.Add("/database/search?track=$c&type=release&artist=$guest&page=1&per_page=20")
        $paths.Add("/database/search?q=$c&type=release&artist=$guest&page=1&per_page=20")
    }
    $paths.Add("/database/search?track=$t&type=release&page=1&per_page=20")
    if ($Core -cne $Title) {
        $paths.Add("/database/search?track=$c&type=release&page=1&per_page=20")
    }
    $paths.Add("/database/search?q=$c&type=release&page=1&per_page=20")
    $paths.Add("/database/search?artist=$a&type=release&page=1&per_page=20")
    Assert-QA ($paths.Count -gt 0 -and $paths.Count -le 16) 'unbounded Discogs stages'
    return @($paths.ToArray())
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
    $paths=Get-OfficialSearchPaths '1000 Years (DJ Version)' 'Kyau & Albert X Maywave' '1000 Years'
    Assert-QA ($paths.Count -eq 8 -and
        $paths[0] -eq '/database/search?q=1000%20Years%20%28DJ%20Version%29&type=release&artist=Kyau%20%26%20Albert%20X%20Maywave&page=1&per_page=20' -and
        $paths[1].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
        $paths[2] -eq '/database/search?track=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[3] -eq '/database/search?q=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[4] -notmatch 'artist=' -and
        $paths[7].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave')) 'full original artist / collaborator discovery plan unqualified'
    $simple=Get-OfficialSearchPaths 'Only You' 'Matt Fax'
    Assert-QA ($simple.Count -eq 5 -and
        $simple[0].Contains('artist=Matt%20Fax') -and
        $simple[2] -notmatch 'artist=') 'normal solo-artist discovery changed'
    Write-Output 'PASS: protected Discogs search-to-edition contract (offline; no secrets)'
    return
}

$cases=@{
    '38012673'=@{Title='Via Balearia';Artist='Estiva'}
    '37332711'=@{Title='1000 Years (DJ Version)';Core='1000 Years';Artist='Kyau & Albert X Maywave'}
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
    $core=if ($case.ContainsKey('Core')) { [string]$case.Core } else { [string]$case.Title }
    $paths=@(Get-OfficialSearchPaths $case.Title $case.Artist $core)
    if ($ReleaseId -eq '37332711') {
        Assert-QA ($paths.Count -eq 8 -and
            $paths[0].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
            $paths[2].Contains('artist=Maywave')) 'original complete artist query was bypassed'
    }
    foreach ($path in $paths) {
        Start-Sleep -Milliseconds 1700
        $stage++
        $search=Get-OfficialJson $path $token
        if (Test-ExactSearchIdentity $search $ReleaseId) { $found=$true; break }
        # Production auto-continues after empty hits, otherwise the user
        # explicitly clicks Search to advance from an irrelevant result.
    }
    Assert-QA $found "exact edition not discovered from full original ARTIST through $($paths.Count) bounded first-page strategies (public ID $ReleaseId)"
    Write-Output "PASS: FULL_ORIGINAL_ARTIST -> AUTHORIZED_SEARCH -> EXACT_EDITION -> TRACKLIST, stage $stage/$($paths.Count), public ID $ReleaseId"
} finally {
    # PowerShell cannot guarantee secure erasure of immutable strings.
    # No token or payload is written to any log, URL, file or artifact.
    Remove-Variable token -ErrorAction SilentlyContinue
}
)) 'unqualified official route'
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
    $paths=Get-OfficialSearchPaths '1000 Years (DJ Version)' 'Kyau & Albert X Maywave' '1000 Years'
    Assert-QA ($paths.Count -eq 8 -and
        $paths[0] -eq '/database/search?q=1000%20Years%20%28DJ%20Version%29&type=release&artist=Kyau%20%26%20Albert%20X%20Maywave&page=1&per_page=20' -and
        $paths[1].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
        $paths[2] -eq '/database/search?track=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[3] -eq '/database/search?q=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[4] -notmatch 'artist=' -and
        $paths[7].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave')) 'full original artist / collaborator discovery plan unqualified'
    $simple=Get-OfficialSearchPaths 'Only You' 'Matt Fax'
    Assert-QA ($simple.Count -eq 5 -and
        $simple[0].Contains('artist=Matt%20Fax') -and
        $simple[2] -notmatch 'artist=') 'normal solo-artist discovery changed'
    Write-Output 'PASS: protected Discogs search-to-edition contract (offline; no secrets)'
    return
}

$cases=@{
    '38012673'=@{Title='Via Balearia';Artist='Estiva'}
    '37332711'=@{Title='1000 Years (DJ Version)';Core='1000 Years';Artist='Kyau & Albert X Maywave'}
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
    $core=if ($case.ContainsKey('Core')) { [string]$case.Core } else { [string]$case.Title }
    $paths=@(Get-OfficialSearchPaths $case.Title $case.Artist $core)
    if ($ReleaseId -eq '37332711') {
        Assert-QA ($paths.Count -eq 8 -and
            $paths[0].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
            $paths[2].Contains('artist=Maywave')) 'original complete artist query was bypassed'
    }
    foreach ($path in $paths) {
        Start-Sleep -Milliseconds 1700
        $stage++
        $search=Get-OfficialJson $path $token
        if (Test-ExactSearchIdentity $search $ReleaseId) { $found=$true; break }
        # Production auto-continues after empty hits, otherwise the user
        # explicitly clicks Search to advance from an irrelevant result.
    }
    Assert-QA $found "exact edition not discovered from full original ARTIST through $($paths.Count) bounded first-page strategies (public ID $ReleaseId)"
    Write-Output "PASS: FULL_ORIGINAL_ARTIST -> AUTHORIZED_SEARCH -> EXACT_EDITION -> TRACKLIST, stage $stage/$($paths.Count), public ID $ReleaseId"
} finally {
    # PowerShell cannot guarantee secure erasure of immutable strings.
    # No token or payload is written to any log, URL, file or artifact.
    Remove-Variable token -ErrorAction SilentlyContinue
}
) { return '' }
    $number=[int]$year
    if ($number -lt 1850 -or $number -gt 2200) { return '' }
    return $year
}

function Get-OfficialSearchPaths([string]$Title,[string]$Artist,[string]$Core='') {
    if ([string]::IsNullOrWhiteSpace($Core)) { $Core=$Title }
    $t=[uri]::EscapeDataString($Title)
    $c=[uri]::EscapeDataString($Core)
    $a=[uri]::EscapeDataString($Artist)
    # Mirrors the native C++ track_search_plan on these qualified fixtures.
    # The FIRST requests always contain the original complete ARTIST.
    $paths=[System.Collections.Generic.List[string]]::new()
    $paths.Add("/database/search?q=$t&type=release&artist=$a&page=1&per_page=20")
    $paths.Add("/database/search?track=$t&type=release&artist=$a&page=1&per_page=20")
    # Only a deliberately joined X collaboration is broadened here;
    # the ampersand in "Kyau & Albert" is part of the same artist name.
    if ($Artist -cmatch '^(.+?) [xX] ([^\r\n]+)$') {
        $guest=[uri]::EscapeDataString($Matches[2])
        $paths.Add("/database/search?track=$c&type=release&artist=$guest&page=1&per_page=20")
        $paths.Add("/database/search?q=$c&type=release&artist=$guest&page=1&per_page=20")
    }
    $paths.Add("/database/search?track=$t&type=release&page=1&per_page=20")
    if ($Core -cne $Title) {
        $paths.Add("/database/search?track=$c&type=release&page=1&per_page=20")
    }
    $paths.Add("/database/search?q=$c&type=release&page=1&per_page=20")
    $paths.Add("/database/search?artist=$a&type=release&page=1&per_page=20")
    Assert-QA ($paths.Count -gt 0 -and $paths.Count -le 16) 'unbounded Discogs stages'
    return @($paths.ToArray())
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
    $paths=Get-OfficialSearchPaths '1000 Years (DJ Version)' 'Kyau & Albert X Maywave' '1000 Years'
    Assert-QA ($paths.Count -eq 8 -and
        $paths[0] -eq '/database/search?q=1000%20Years%20%28DJ%20Version%29&type=release&artist=Kyau%20%26%20Albert%20X%20Maywave&page=1&per_page=20' -and
        $paths[1].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
        $paths[2] -eq '/database/search?track=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[3] -eq '/database/search?q=1000%20Years&type=release&artist=Maywave&page=1&per_page=20' -and
        $paths[4] -notmatch 'artist=' -and
        $paths[7].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave')) 'full original artist / collaborator discovery plan unqualified'
    $simple=Get-OfficialSearchPaths 'Only You' 'Matt Fax'
    Assert-QA ($simple.Count -eq 5 -and
        $simple[0].Contains('artist=Matt%20Fax') -and
        $simple[2] -notmatch 'artist=') 'normal solo-artist discovery changed'
    Write-Output 'PASS: protected Discogs search-to-edition contract (offline; no secrets)'
    return
}

$cases=@{
    '38012673'=@{Title='Via Balearia';Artist='Estiva'}
    '37332711'=@{Title='1000 Years (DJ Version)';Core='1000 Years';Artist='Kyau & Albert X Maywave'}
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
    $core=if ($case.ContainsKey('Core')) { [string]$case.Core } else { [string]$case.Title }
    $paths=@(Get-OfficialSearchPaths $case.Title $case.Artist $core)
    if ($ReleaseId -eq '37332711') {
        Assert-QA ($paths.Count -eq 8 -and
            $paths[0].Contains('artist=Kyau%20%26%20Albert%20X%20Maywave') -and
            $paths[2].Contains('artist=Maywave')) 'original complete artist query was bypassed'
    }
    foreach ($path in $paths) {
        Start-Sleep -Milliseconds 1700
        $stage++
        $search=Get-OfficialJson $path $token
        if (Test-ExactSearchIdentity $search $ReleaseId) { $found=$true; break }
        # Production auto-continues after empty hits, otherwise the user
        # explicitly clicks Search to advance from an irrelevant result.
    }
    Assert-QA $found "exact edition not discovered from full original ARTIST through $($paths.Count) bounded first-page strategies (public ID $ReleaseId)"
    Write-Output "PASS: FULL_ORIGINAL_ARTIST -> AUTHORIZED_SEARCH -> EXACT_EDITION -> TRACKLIST, stage $stage/$($paths.Count), public ID $ReleaseId"
} finally {
    # PowerShell cannot guarantee secure erasure of immutable strings.
    # No token or payload is written to any log, URL, file or artifact.
    Remove-Variable token -ErrorAction SilentlyContinue
}
