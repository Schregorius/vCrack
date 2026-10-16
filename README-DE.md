# vcrack 1.1.0

[English](README.md) | **Deutsch**

**Regelbasierte VeraCrypt-Passwort-Wiederherstellung.** vcrack erzeugt Satz-Permutationen
aus einer Wortliste nach konfigurierbaren Regeln und streamt sie direkt in
[`hashcat`](https://hashcat.net). Gedacht für den Fall, dass man das **eigene**
VeraCrypt-Passwort vergessen hat und sich noch an die *Bausteine* (Wörter, Namen,
Zahlen) erinnert, aber nicht an deren genaue Reihenfolge.

> ⚠️ Nur für die Wiederherstellung eigener Daten gedacht. Der Einsatz gegen fremde
> Volumes ohne Erlaubnis ist illegal.

Der Trick: Statt blind alle Zeichen durchzuprobieren (aussichtslos), wird nur der
**sinnvolle Raum** aus bekannten Wörtern + Regeln erzeugt. Das verkleinert den
Suchraum von „unendlich" auf „machbar".

---

## Was es kann

- **Nativer C++-Generator** (keine Laufzeitumgebung nötig), erzeugt Permutationen als Stream direkt in hashcat (`-a 0` stdin-Modus) – nichts wird auf die Platte geschrieben.
- **Regelsystem** (`rules.txt`): Länge, Positionsregeln, verbotene Nachbarschaften, Ausschlüsse usw. – zur Laufzeit geladen, **kein Neukompilieren** bei Regeländerungen.
- **Alles-in-einem `estimate`**: Monte-Carlo-Keyspace mit Fehlerspanne (±%) in <1 s, die Verteilung je Länge, die gemessenen H/s je Hash und die ETA je Modus – ein Befehl (ersetzt die getrennten Schritte `sample` + `bench`; beide gibt es weiter als Low-Level-Befehle).
- **Header-Extraktion** (`vcrack header --src <volume>`): zieht den VeraCrypt-Header aus einem Volume/Container in die Zieldatei; auch in der GUI.
- **Resume & Checkpoints**: Fortschritt wird pro Block gespeichert; innerhalb eines Blocks spätestens alle paar Minuten (`--checkpoint`). Abbruch verliert nie mehr als dieses Intervall.
- **Intelligente Wiederaufnahme bei JEDER Änderung** (Wörter *oder* Regeln): der Fortschritt wird als *welche Kandidaten tatsächlich getestet wurden* geführt. Wort hinzufügen → nur Kombinationen mit diesem Wort werden getestet. Wort entfernen oder Regel verschärfen → alles noch Gültige war schon abgedeckt, es wird nichts erneut getestet. Regel lockern → nur die neu freigeschalteten Kombinationen. Eine Regeländerung verwirft nie Fortschritt; jeder fertige Block speichert seine eigene Regelmenge.
- **Such-Hash-Cache**: `count`, `sample` und `estimate` teilen sich Ergebnisse, solange Wörter/Regeln/Längen unverändert sind.
- **Sicherheits-Check**: Startet nur, wenn hashcat tatsächlich ein GPU-Device sieht, und markiert einen Block **nur** nach einem echten (erschöpften) hashcat-Lauf als erledigt – kein Scheinfortschritt bei fehlender GPU.
- **Grafische Oberfläche** (optional): lokale Web-UI im Win11-Fluent-Stil, startet in einem eigenständigen Fenster.
- **Mehrsprachige GUI**: alle Texte liegen als Variablen in `text_variable_specs.csv`; Übersetzungen in `<Sprache>.lng` (mitgeliefert: Deutsch, English). Umschaltbar im Kopf und in den Einstellungen. Ein **Label-Hash** erkennt veraltete Übersetzungen.
- **Einstellungen-Tab**: globale Optionen in `vcrack.config` (Sprache, Design, erkannte Dateiendungen, Standard-Header-Bytes, Standardwerte für neue Ziele) und zieldatei-spezifische Optionen in der `.vc`-Datei.
- **Zieldatei-`.vc`**: pro Ziel eine Datei (`<Name>-<Bytes>.vc`) mit Header-Pfad, Einstellungen, Wortliste, Regeln, Statistik und Fortschritts-Verweis. Wird beim Wählen eines Ziels automatisch angelegt/geladen.
- **Wortliste als Liste**: Add/Edit/Remove statt Textfeld, jedes Wort mit Byte-Index und „benutzt"-Markierung.

> **Wortzahl-Grenzen:** max. **255** Wörter (Byte-Index 0–254). Der Generator-Kern nutzt 64-Bit-Masken, daher sind aktuell höchstens **64 aktive** Wörter möglich; die GUI weist darauf hin.

Die Format-Spezifikationen (`.vc`, `vcrack.config`, `.lng`, Text-Variablen/Label-Hash) und der Architektur-Plan liegen im Ordner [`docs/`](docs/).

---

## Voraussetzungen

- Windows mit **hashcat 7.x** (`hashcat.exe` im selben Ordner) und funktionierender GPU (CUDA/OpenCL).
- Zum Bauen: **MSVC** (Visual Studio 2022/2026, „x64 Native Tools").
- Für die GUI: **Python 3** (nur Standardbibliothek).

## Dateien an ihren Platz

vcrack läuft **im hashcat-Ordner** (neben `hashcat.exe`). Dorthin gehören:

```
hashcat.exe
vcrack.cpp          <- Quellcode
build.bat           <- Build-Skript
rules.txt           <- deine Regeln (Vorlage: rules.example.txt)
list.txt            <- deine Wortliste (ein Wort pro Zeile)
<volume>.tc         <- VeraCrypt-Header der Zieldatei
vcrack-gui.bat      <- GUI-Starter
gui\                <- GUI-Server + Seite
```

**Header extrahieren**: macht vcrack für dich —

```
vcrack header --src E:\meine_videos.hc --target Fvc_header.tc
```

Kopiert die ersten 512 KB der Quelle (Volume-Datei oder Container) in die Zieldatei;
geprüft werden nur die ersten 512 Byte, 512 KB decken aber auch versteckte Header ab.
In der GUI steht das Gleiche unter **Cockpit → Header auslesen**. Das Original bleibt
unverändert.

## Einrichten / Bauen

Beim ersten Mal einfach:

```
install.bat
```
Baut `vcrack.exe`, legt `rules.txt` und `list.txt` aus der Vorlage an (falls nicht vorhanden)
und prüft hashcat / GPU / Python. Für einen reinen Neu-Build später `build.bat`
(oder in der „x64 Native Tools Command Prompt": `cl /nologo /O2 /EHsc /std:c++17 /Fe:vcrack.exe vcrack.cpp`).

---

## Befehle

```
vcrack rules       aktive Regeln anzeigen (aus rules.txt)
vcrack estimate    Alles-in-einem: Keyspace + Verteilung je Laenge + H/s je Hash + ETA
vcrack count       Keyspace exakt zaehlen (langsamer, mehrkernig, gecacht)
vcrack sample      Low-Level: nur Keyspace-Schaetzung (Monte-Carlo, +/- Fehler)
vcrack bench       Low-Level: H/s je Hash messen und cachen
vcrack header      Header aus einem VeraCrypt-Volume in die Zieldatei extrahieren
vcrack gen         Kandidaten nach stdout (zum Testen/Pipen)
vcrack run         Angriff: Modi x Laenge x Startwort/-woerter, mit Resume
```

`estimate` ist der Befehl, den man normalerweise vor einem Lauf braucht – er fasst
zusammen, was frueher `sample` und `bench` getrennt gemacht haben: Keyspace, die
Aufteilung je Laenge, die gemessenen H/s je Hash und die daraus folgende Laufzeit je Modus.

### Optionen

| Option | Bedeutung |
|---|---|
| `--words F` | Wortliste (Standard `list.txt`) |
| `--rules F` | Regeldatei (Standard `rules.txt`) |
| `--target F` | VeraCrypt-Header (Standard `Fvc_header.tc`) |
| `--min N` / `--max N` | Wortanzahl pro Kandidat (Standard aus `rules.txt`) |
| `--hash H` | nur ein Hash: `SHA-512`, `Whirlpool`, `Streebog`, `SHA-256` |
| `--size S` | nur eine Cipher-Größe: `512`, `1024`, `1536` (Einzel/2er/3er-Kaskade) |
| `--device D` | `gpu` (Standard), `cpu`, `both` |
| `--workload N` | hashcat `-w` 1..4 (Standard 3; höher = mehr GPU, weniger Desktop) |
| `--checkpoint SEK` | run-Fortschritt spätestens alle SEK Sekunden sichern (Standard 300, 0 = aus) |
| `--temp N` | GPU-Notaus bei N °C (Standard 90) |
| `--samples N` | Stichproben für sample/estimate (Standard 300000) |
| `--rebench` | H/s neu messen statt aus Cache |
| `--src F` | Quelle (Volume/Container) für `header` |
| `--bytes N` | Bytes für `header` (Standard 524288 = 512 KB) |
| `--json` | Maschinen-Ausgabe (für die GUI) |

### Typischer Ablauf

```
vcrack rules
vcrack estimate --max 8      # Keyspace + Verteilung + H/s + realistische Laufzeiten
vcrack run --hash SHA-512 --size 512 --max 8
```

---

## Regeldatei (`rules.txt`)

Zeilen mit `#` sind Kommentare. Groß/Klein egal für Regel-Wörter. Die englischen
Schlüsselwörter sind primär; die früheren deutschen funktionieren weiter als Alias.

| Regel | Bedeutung | Alias |
|---|---|---|
| `length A B` | Kandidat besteht aus A..B Wörtern | `laenge` |
| `frompos N : w1 w2 ...` | diese Wörter nur ab Position N | `abpos` |
| `forbid-adjacent L.. -> R..` | Übergang verbieten (wenn links L, dann rechts R nicht) | `nachbarn-verboten` |
| `exclusive w1 w2 ...` | höchstens eines dieser Wörter pro Kandidat | `exklusiv` |
| `not-at-end N : w1 ...` | nicht in den letzten N Positionen | `nicht-am-ende` |
| `max-one-ever` | spezielle „ever"-Kombinationslogik | `max-ein-ever` |

Eine neutrale Vorlage liegt als **`rules.example.txt`** bei. Kopiere sie nach
`rules.txt` und passe sie an deine Wörter an. Maximal **64 Wörter** in der Liste
(interne Bitmasken).

---

## Resume / Checkpoints

- Durchlauf: pro **Modus** → pro **Länge** → pro **Startwort** = ein Block. Bei langen
  Längen (≥ 9) wird jeder Block zusätzlich nach dem **zweiten Wort** aufgeteilt – so
  schreitet der Fortschritt bei den riesigen Längen 9/10 in vielen kleinen,
  wiederaufnehmbaren Schritten voran.
- Jeder fertige Block wird als `DONE`-Zeile in `vcrack_state.txt` gespeichert.
- Innerhalb eines Blocks sichert `--checkpoint` den Offset als `PART`-Zeile.
- `vcrack run` erneut starten setzt automatisch fort; Erledigtes wird übersprungen.
- Wortliste/Länge ändern: es wird nur das **Neue** getestet (Differenz), nie von vorne.
- **Regeländerung** nimmt ebenfalls wieder auf: jeder fertige Block speichert seine eigene
  Regelmenge. Beim nächsten Lauf wird jeder noch gültige Kandidat, den ein bereits erledigter
  Block schon erzeugt hat, übersprungen; nur wirklich neue Kandidaten werden getestet. Verschärfen
  testet nichts Neues, Lockern nur das neu Freigeschaltete. (Einmaliger Hinweis: Blöcke, die eine
  *ältere* vcrack-Version ohne gespeicherten Regeltext fertiggestellt hat, können nur dann
  angerechnet werden, wenn die aktuellen Regeln zur Signatur dieses Blocks passen – sonst werden
  sie einmalig neu geprüft.)

Zustand/Cache (werden automatisch angelegt, **nicht** im Repo):
`vcrack_state.txt`, `vcrack_cache.txt`, `vcrack_bench.txt`.

---

## Grafische Oberfläche

```
vcrack-gui.bat
```
Startet den lokalen Server (`gui\gui_server.py`, nur Python-Standardbib) und öffnet
ein **eigenständiges Fenster** (Chrome/Edge im App-Modus). Reiter: **Cockpit**
(Ziel-Auswahl, Header auslesen, Start/Stop, Live-Kacheln – Tempo, Temperatur,
Auslastung, Lüfter, GPU-Takt, Block-Restzeit – und Fortschrittsbalken),
**Statistik** (ein Klick → Keyspace + Verteilung je Länge + Laufzeiten),
**Regeln**, **Wortliste**. Konsolenfenster offen lassen – Schließen beendet den Server.

---

## hashcat-VeraCrypt-Modi (Referenz)

Endung des `-m`-Codes = Cipher-Kaskade: `…1` = Einzel-Cipher (Standard), `…2` = 2er-,
`…3` = 3er-Kaskade. vcrack probiert sie nach Wahrscheinlichkeit durch.

| Hash | 512 | 1024 | 1536 |
|---|---|---|---|
| SHA-512 | 13721 | 13722 | 13723 |
| Whirlpool | 13731 | 13732 | 13733 |
| Streebog | 13771 | 13772 | 13773 |
| SHA-256 | 13751 | 13752 | 13753 |

Bei VeraCrypt bestimmt der **Hash** (PBKDF2-Iterationen) das Tempo, nicht die
Cipher-Kaskade – darum misst vcrack ein H/s je Hash und überträgt es.

---

## Sicherheit / nicht im Repo

Die `.gitignore` hält **sensible und erzeugte Dateien draußen**: `list.txt`,
`list.meta`, `rules.txt`, der Volume-Header (`*.tc`, `*vc_header*`, `*.hf`),
`geknackt.txt`, die zieldatei-bezogenen Dateien (`vcrack.config`, `*.vc`, `*.state`)
sowie Fortschritt/Cache (`vcrack_*.txt`) und `*.exe`. Lade niemals deine echte
Wortliste, die `.vc`-Datei oder den Header hoch – sie verraten dein Passwort.
Die App-Ressourcen `text_variable_specs.csv` und die `*.lng` enthalten **keine**
Nutzerdaten und gehören ins Repo.

## Lizenz

MIT – siehe [LICENSE](LICENSE).
