# Music Metadata Studio

Shared, deterministic metadata-normalization engine for foobar2000 and DJ Library.

## Scope

This repository is deliberately separate from `rekordbox-mytag-sync`. It owns the normalization rule model, preview engine, deterministic tests, and the future foobar2000 normalization component. It does **not** own Rekordbox MyTag synchronization.

## Product contract

**One engine – two surfaces – one rule set.**

- **Core:** immutable metadata input -> ordered normalization proposals.
- **foobar2000 adapter:** selection/context-menu/preview and, only after explicit approval, SDK-based tag writes.
- **DJ Library adapter:** analysis/review over the same core and rules.
- **Bridge:** `foo_dj_library_bridge` remains a read-only snapshot producer.

Analysis never writes metadata. Original values and proposed canonical values remain separate.

## Safety classes

- `SAFE`: deterministic, non-semantic cleanup that may later be eligible for a "safe only" apply action.
- `CONFIDENT`: strongly supported canonicalization that still deserves user visibility.
- `REVIEW`: ambiguous or semantic change requiring an explicit decision.

The initial development phase implements only analysis/preview behavior. Real tag writing is intentionally out of scope until the rule engine, trace output, stale-input protection and deterministic tests are qualified.

## Verbindliche Anforderungen und Entwicklungsstand

Das [zentral geführte Anforderungsregister](docs/REQUIREMENTS-REGISTER.md)
enthält alle bisher bestätigten Workflow-, UX-, Konfigurations-, Regel-,
Quellen-, CUE-, Dateiorganisations- und Sicherheitswünsche mit stabilen IDs,
Status und verknüpften Abnahmekriterien. Ein
[maschinell geprüftes ID-Verzeichnis](docs/REQUIREMENTS-IDS.lock)
verhindert unbemerkte Löschungen; Details bleiben in den thematischen Docs
und offenen Issues #33, #46–#51. Neuer Code nennt die betroffenen IDs.

## Repository layout

- `include/djmeta/` – public core API.
- `src/core/` – normalization engine.
- `rules/` – versioned canonical rule set and schema.
- `tests/` – deterministic golden/unit tests.
- `docs/` – architecture and integration contracts.
- `.github/workflows/` – reproducible CI.


## Crates: ein sichtbarer Track, manuelle Quellübernahme (Draft PR #45)

Crates ist neben Beatport/Discogs/Deezer eine priorisierte DJ-Quelle.
Bei inhaltlich gleich gut belegten Treffern ist die voreingestellte
Anzeigereihenfolge Beatport, Crates, Discogs, Deezer, MusicBrainz. Ein
Mix-/Titelkonflikt wird niemals von dieser Reihenfolge überstimmt.
Die Reihenfolge ist in foobar unter **Preferences → Tools → Music Metadata
Studio → Online providers** mit **Move up / Move down** veränderbar und
wird pro foobar-Profil gespeichert (Apply/OK; Reset setzt nur die
Formularauswahl zurück, bis Apply erfolgt). Sie entscheidet ausschließlich
bei gleich guter, bereits geprüfter Match-Evidenz über die Anzeige,
nicht über einen automatischen Quellenabruf oder Schreibfreigaben.
Unter **Workspace** bleibt der initiale Suchmodus wählbar; im
**Find & compare**-Fenster kann er jederzeit manuell gewechselt werden.
Crates wird aktuell gemeinsam mit Beatport über die ausdrücklich gestartete
Zwischenablage-Übernahme bedient. Deezer steht in der Prioritätenliste,
besitzt im laufenden Draft jedoch noch keine native Live-Suche. Für
gleichzeitige Suchen über alle fünf Anbieter gibt es derzeit keinen
freigegebenen Workflow.
Die öffentliche, ohne Login erreichbare Crates-Trackseite zeigt unter
anderem Titel/Mix, Künstler, Remix-Credits, Label, Release, Genre, BPM,
Tonart und Dauer. Der Browserhelfer
[scripts/crates-copy-visible-track-facts.js](scripts/crates-copy-visible-track-facts.js)
ist **kein Hintergrund-Crawler**: Er liest nur **eine vom Nutzer geöffnete
Track-Detailseite** aus dem sichtbaren DOM und kopiert
\`@crates-public-track\` mit einer exakten Crates- und Beatport-Track-ID.

Zur Verwendung später den Skriptinhalt als \`javascript:\`-Lesezeichen im
normalen Browser einrichten, auf einer einzelnen Crates-Trackseite aufrufen,
in foobar **Beatport + Crates** und **Import clipboard / Import Beatport**
wählen. Originale Tags werden ausschließlich mit den sichtbaren
Quellangaben verglichen. Alle Vorschläge bleiben **read-only**. Die
angezeigte Crates-*Date* wird nicht in DATE/RELEASEDATE verwandelt, weil
ihre exakte Bedeutung nicht separat qualifiziert ist. Beatport-Login,
Cookies, private APIs, Navigation, Dateischreibaktionen und zusätzliche
Software sind nicht erforderlich. Das endgültige installierte
Windows/foobar UX-Verhalten ist noch nicht manuell abgenommen.


## Crates: exakte Release-Trackliste mit Katalognummer (Draft PR #45)

Die vom Nutzer bereits geöffnete öffentliche Release-Seite lässt sich mit
[scripts/crates-copy-visible-release-facts.js](scripts/crates-copy-visible-release-facts.js)
als Lesezeichen einmalig in die Zwischenablage übernehmen. Das Format
`@crates-public-release` enthält zwei unabhängig sichtbare, gleichlautende
Crates-/Beatport-Release-IDs, ALBUM, LABEL, optional CATALOGNUMBER und die
vollständige sichtbare Trackliste mit Track-IDs, Tracknummern, Künstlern,
Remixern, BPM, Tonart und Dauer. Der Offline-Parser verweigert wechselnde
Release-IDs, Duplikate, falsche Tracknummern und unbekannte Felder. Die
foobar-Vorschau zeigt Album/Track-Abgleich nur bei eindeutiger physischer
Titel-/Künstler-/Mix-/Dauerzuordnung. Ungesicherte CUE-Zuordnungen, jedes
automatische Schreiben und das unqualifizierte Crates-Datum bleiben gesperrt.
Der Nutzer muss die öffentliche Seite selbst öffnen und den Helfer auslösen.

## Current qualification (23 October 2026)

Development continues in Draft PR #45; this is not a merged or released component.

- The foobar2000 workspace is modeless and single-instance; the standard
  component build is **read-only**. Source identity, stale snapshots,
  explicit decisions and no-background-request behavior remain mandatory.
- Deterministic online candidate/release review, metadata normalization,
  external/embedded CUE inspection and destination previews share the
  versioned core. Provider results never authorize an automatic write.
- The private, **opt-in** `-EnablePhysicalWrites` local acceptance build adds
  exact selected physical tag-value writes via the foobar SDK and value-only
  external CUE writes. Both require preview, final human confirmation,
  independent source revalidation and exact post-write readback. Embedded
  CUE writes, FILE-reference edits and file moves are not enabled by this
  gate.
- GitHub Actions [foobar host qualification](https://github.com/lxsdd/music-metadata-studio/actions/runs/39962237786)
  passed on Win32 and x64 at commit `a066166e77f767563f434e941913d3b71274b3e2`.
  Disposable official foobar instances exercised true physical tag no-op,
  selected tag writes and an external CUE transaction while confirming
  unchanged audio bytes. [Core CI](https://github.com/lxsdd/music-metadata-studio/actions/runs/39962237899)
  passed on Ubuntu and Windows.
- These host tests validate the writer backends, not human-visible
  placement, accessibility, provider account interaction or the final UI
  confirmation workflow. A private installed-foobar UI acceptance on
  disposable files is still the next release gate; no public binaries,
  merge or release are authorized.

See [private acceptance build](docs/LOCAL_ACCEPTANCE_BUILD.md),
[modeless workspace gate](docs/MODELESS-WORKSPACE-ACCEPTANCE.md), and
[external CUE value writer](docs/EXTERNAL-CUE-VALUE-WRITER.md).
