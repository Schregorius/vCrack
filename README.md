# vcrack 1.0.0

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
- **Keyspace-Schätzung per Monte-Carlo** mit Fehlerspanne (±%), in <1 s, inkl. DOS-Dashboard und Verteilung pro Länge.
- **Laufzeit-Schätzung** pro hashcat-Modus (misst H/s, rechnet ETA).
- **Resume & Checkpoints**: Fortschritt wird pro Block gespeichert; innerhalb eines Blocks spätestens alle paar Minuten (`--checkpoint`). Abbruch verliert nie mehr als dieses Intervall.
- **Intelligente Wiederaufnahme bei Änderungen**: Wort hinzufügen → es werden nur die *neuen* Kombinationen getestet. Wort entfernen / Regel verschärfen → übersprungen. Nichts wird doppelt gerechnet.
- **Such-Hash-Cache**: `count`, `sample` und `estimate` teilen sich Ergebnisse, solange Wörter/Regeln/Längen unverändert sind.
- **Sicherheits-Check**: Startet nur, wenn hashcat tatsächlich ein GPU-Device sieht, und markiert einen Block **nur** nach einem echten (erschöpften) hashcat-Lauf als erledigt – kein Scheinfortschritt bei fehlender GPU.
- **Grafische Oberfläche** (optional): lokale Web-UI im Win11-Fluent-Stil, startet in einem eigenständigen Fenster.

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

**Header extrahieren** (die ersten 512 KB des VeraCrypt-Volumes genügen; nur die ersten 512 Byte werden geprüft). Beispiel für eine Containerdatei:
```
copy /b volume.hc Fvc_header.tc   # oder die ersten KB des Volumes/Partition sichern
```
Siehe hashcat-Doku zu den VeraCrypt-Modi für Details zur Header-Gewinnung.

## Bauen

```
build.bat
```
Erzeugt `vcrack.exe`. (Alternativ in der „x64 Native Tools Command Prompt":
`cl /nologo /O2 /EHsc /std:c++17 /Fe:vcrack.exe vcrack.cpp`)

---

## Befehle

```
vcrack rules       aktive Regeln anzeigen (aus rules.txt)
vcrack sample      Keyspace schnell schaetzen (Monte-Carlo, +/- Fehler, Dashboard)
vcrack count       Keyspace exakt zaehlen (langsamer, mehrkernig, gecacht)
vcrack estimate    Keyspace x H/s -> Laufzeit-Tabelle pro Modus
vcrack bench       H/s je Hash messen und cachen
vcrack gen         Kandidaten nach stdout (zum Testen/Pipen)
vcrack run         Angriff: Modi x Laenge x Startwort, mit Resume
```

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
| `--checkpoint SEK` | Zwischenspeicherung des run-Fortschritts spätestens alle SEK Sekunden (Standard 300, 0 = aus) |
| `--temp N` | GPU-Notaus bei N °C (Standard 90) |
| `--samples N` | Stichproben für sample/estimate (Standard 300000) |
| `--rebench` | H/s neu messen statt aus Cache |
| `--json` | Maschinen-Ausgabe (für die GUI) |

### Typischer Ablauf

```
vcrack rules
vcrack bench
vcrack estimate --max 8      # realistische Laufzeiten ansehen
vcrack run --hash SHA-512 --size 512 --max 8
```

---

## Regeldatei (`rules.txt`)

Zeilen mit `#` sind Kommentare. Groß/Klein egal für Regel-Wörter.

| Regel | Bedeutung |
|---|---|
| `laenge A B` | Kandidat besteht aus A..B Wörtern |
| `abpos N : w1 w2 ...` | diese Wörter nur ab Position N |
| `nachbarn-verboten L.. -> R..` | Übergang verbieten: wenn links L, dann rechts R nicht |
| `exklusiv w1 w2 ...` | höchstens eines dieser Wörter pro Kandidat |
| `nicht-am-ende N : w1 ...` | diese Wörter nicht in den letzten N Positionen |
| `max-ein-ever` | spezielle „ever"-Kombinationslogik (Beispiel für Projektlogik) |

Eine neutrale Vorlage liegt als **`rules.example.txt`** bei. Kopiere sie nach
`rules.txt` und passe sie an deine Wörter an.

Maximal **64 Wörter** in der Liste (interne Bitmasken).

---

## Resume / Checkpoints

- Durchlauf: pro **Modus** → pro **Länge** → pro **Startwort** = ein Block.
- Jeder fertige Block wird als `DONE`-Zeile in `vcrack_state.txt` gespeichert.
- Innerhalb eines Blocks sichert `--checkpoint` den Offset als `PART`-Zeile.
- `vcrack run` erneut starten setzt automatisch fort; Erledigtes wird übersprungen.
- Wortliste/Länge ändern: es wird nur das **Neue** getestet (Differenz), nie von vorne.
- Regeländerung ändert die Regel-Signatur → sicherheitshalber Neuberechnung.

Zustand/Cache (werden automatisch angelegt, **nicht** im Repo):
`vcrack_state.txt`, `vcrack_cache.txt`, `vcrack_bench.txt`.

---

## Grafische Oberfläche

```
vcrack-gui.bat
```
Startet den lokalen Server (`gui\gui_server.py`, nur Python-Standardbib) und öffnet
ein **eigenständiges Fenster** (Chrome/Edge im App-Modus). Reiter: **Cockpit**
(Start/Stop, Live-Status), **Statistik** (Keyspace + Laufzeit), **Regeln**,
**Wortliste**. Konsolenfenster offen lassen – Schließen beendet den Server.

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
`rules.txt`, der Volume-Header (`*.tc`, `*vc_header*`), `geknackt.txt`, der
Fortschritt/Cache (`vcrack_*.txt`) sowie `*.exe`. Lade niemals deine echte
Wortliste oder den Header hoch – sie verraten dein Passwort.

## Lizenz

MIT – siehe [LICENSE](LICENSE).
