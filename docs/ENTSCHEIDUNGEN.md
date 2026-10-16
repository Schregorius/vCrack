# Getroffene Entscheidungen

Stand: 2026-10-16. Alle offenen Fragen aus `FRAGEN.md` wurden mit dem User
durchgegangen und entschieden. Diese Tabelle ist jetzt verbindlich.

| # | Thema | Entscheidung (final) |
|---|-------|----------------------|
| A1 | Fortschritt & `.vc` | **Fortschritt wird in die `.vc` eingebettet.** Export/Import baut die State-Zeilen beim Lauf rein/raus. (vorher provisorisch: referenzieren) |
| A2 | Arbeitsordner / Migration | Arbeitsordner ist ab jetzt **`E:\vcrack`** (ganzer hashcat-Ordner dorthin kopiert). **Kein `migrate` nötig.** Pfad-Referenzen entsprechend. |
| A3 | Engine heute Nacht | Blieb unangetastet — korrekt. Engine-Änderungen (A1-Einbettung) folgen bewusst in einem eigenen Schritt. |
| B1 | `.vc`-Format | **Text/INI.** |
| B2 | `vcrack.config`-Format | **Text/INI.** |
| C1 | Header-Namensschema | **Neues `[name].[ext]_header.hf` nur für neue Header**; bestehende `.tc`/`.hc` weiter unterstützt. |
| C2 | Dropdown-Auto-Update | Header **nur neu auslesen, wenn keine Header-Datei existiert**, sonst nur `.vc` laden. |
| C3 | Default-Dateiendungen | **`.hc`, `.tc`** (in den Einstellungen änderbar). |
| D1 | Sprachen zum Start | **Deutsch + English.** |
| D2 | Startsprache | Deutsch, in den Einstellungen umschaltbar. |
| D3 | Label-Hash | **16 Hex-Zeichen** (gekürzter SHA-256) genügen. |
| E1 | `used`-Flag-Semantik | **used=1 erst bei Block-Ende** (nicht schon bei Run-Start). Ableitung aus abgeschlossenen Blöcken. |
| E2 | Wort-Index | 0-basiert, intern. |
| F1 | Wort-Limit | **Bei 64 aktiven Wörtern bleiben** (64-Bit-Masken). Byte-Index-Feld bis 255 vorbereitet, aber kein Engine-Umbau. |
| G1 | `vcrack.config`-Ort | **App-/Arbeitsordner `E:\vcrack`** (portabel). |
| G2 | Regeln-Tab | Vorerst Textfeld; **Baustein-UI später als eigenes Issue.** |

## Daraus folgende Umsetzung (siehe FRAGEN.md → Umsetzungsliste)
- A1 ist der größte offene Punkt: Fortschritt in die `.vc` einbetten (Engine + Server).
- E1 ändert die heute Nacht gebaute „bei Run-Start"-Markierung auf „bei Block-Ende".
- Arbeitsordner-Umstellung auf `E:\vcrack` (geräteseitig, sobald verbunden).
