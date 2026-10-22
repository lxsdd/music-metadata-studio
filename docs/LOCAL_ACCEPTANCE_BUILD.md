# Lokaler, nichtöffentlicher Endabnahme-Build (Windows)

**Entwicklungsmodus; keine öffentliche Binär-Freigabe.** Dieses Verfahren liefert ein
persönlich auf dem eigenen Windows-Rechner kompiliertes foobar2000-Paket für die
gesonderte Endabnahme, ohne GitHub-Actions-Artefakte, GitHub-Releases oder lokale
Git-Repositories anzulegen. Eine Rechtefreigabe zur Weitergabe ist damit **nicht**
verbunden.

## Voraussetzungen

- Windows 10/11; Visual Studio 2022 **C++ Build Tools** mit MSBuild / v143
  (PowerShell aus **Developer PowerShell for VS 2022** starten)
- 7-Zip (7z.exe) und Netzwerkzugriff auf GitHub sowie foobar2000.org
- Genügend freier temporärer Festplattenspeicher für SDK, Win32/x64-Builds
- Ein vollständig bestimmter **40-stelliger lowercase Commit-SHA**, der explizit
  für den privaten Abnahme-Build ausgewählt wurde
- Ein vom Nutzer ausdrücklich gewählter, neuer Ausgabeordner (keine Installation
  oder Schreiboperation im foobar-Profil, der Musikbibliothek oder in Tags)

## Anwendung, ohne ein Repo lokal zu klonen

Im Developer PowerShell mit dem **freigegebenen** SHA (kein Branchname) zuerst
das Skript dieses exakten Commits als Textdatei laden. Beispiel mit Platzhalter
statt eines möglicherweise veralteten Commits:

```powershell
$sha = '<40-STELLIGER-COMMIT-SHA>'
$helper = Join-Path $env:TEMP 'mms-local-acceptance-build.ps1'
Invoke-WebRequest -UseBasicParsing -Uri "https://raw.githubusercontent.com/lxsdd/music-metadata-studio/$sha/scripts/local-acceptance-build.ps1" -OutFile $helper
# Preflight erfordert keine ZIP-/SDK-Downloads:
& $helper -PreflightOnly
# Nur nach erfolgreich qualifiziertem SHA und expliziter lokaler Ausgabe:
& $helper -SourceSha $sha -OutputDirectory 'C:\Temp\MMS-PrivateAcceptance'
```

Der Helper lädt das exakte Commit-Archiv per HTTPS von `codeload.github.com`,
prüft dessen Commit-Präfix, erstellt Win32-/x64-DLLs mit den unveränderten
Projekt-Buildskripten und dem im Projekt **SHA-256-pinned SDK 2026-10-01**,
nutzt das bestehende Original-Paketierungsskript, verifiziert PE-Maschinenkennung
und SHA-256 sowie Git-SHA-Eintrag im Build-Manifest und kopiert ausschließlich
Komponente, Manifest und Prüfsummendatei in:

`<OutputDirectory>\MMS-local-<40-digit SHA>\`

Bereits existierende Zielordner werden **nicht überschrieben**. Der GUID-basierte
temporäre Quell-/SDK-/Buildordner wird im `finally` aufgeräumt. Bei Fehlern
kann zur Diagnose ein leeres/teilweise gefülltes **neues** Ausgabeziel verbleiben;
es werden keine vorhandenen Daten gelöscht.

**Kein automatisches Installieren, Patchen, Tagging, Datenbank- oder Audiozugriff.**
Das Paket darf erst nach separater Freigabe und Prüfung im Endgerät installiert
werden. Die früheren Hilfsskript-Läufe [39435120160](https://github.com/lxsdd/music-metadata-studio/actions/runs/39435120160)
und [39438568386](https://github.com/lxsdd/music-metadata-studio/actions/runs/39438568386)
qualifizierten den **Vorgänger**, nicht die Studio-spezifischen Projektnamen.
Die aktuelle Studio-Version hat einen eigenen Win32/x64-Preflight-Gate in
der foobar-CI. Eine **vollständige lokale Paketerstellung** auf dem letzten
freigegebenen SHA und die echte Endgerätabnahme bleiben separat nachzuweisen.
Keine Binär-Artefakte werden aus einem öffentlichen PR bereitgestellt. Eine Installation
und die **echte Endgerätabnahme auf deinem Windows-Rechner** bleiben separat
und sind nicht durch GitHub-CI ersetzbar.

Die bestehenden öffentlichen Sicherheits-PRs verhindern parallel, dass ein
Github-Actions-Build des gleichen Quellcodes als öffentlich abrufbare DLL
hochgeladen wird. Auf geschütztem `main` wird dies erst nach separater
ausdrücklicher Merge-Freigabe wirksam.


## Studio-spezifische Paketidentität

Diese Version prüft ausdrücklich `foo_music_metadata_studio.vcxproj`, die
gebauten `foo_music_metadata_studio.dll` für Win32 und x64 und das Paket
`foo_music_metadata_studio-<VERSION>.fb2k-component`. Sie darf weder das
alte `foo_dj_metadata_normalizer`-Paket erwarten noch es als Studio-Build
umbenennen. SHA-Pin, Manifest-Prüfung, Architektur-Kennungen und `SHA256SUMS`
bleiben verpflichtend. Der Vorgang installiert keine Komponente und
schreibt weder Musikdateien noch CUEs oder Metadaten.
