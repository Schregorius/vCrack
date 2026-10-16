# Getroffene Entscheidungen (provisorisch, jederzeit kippbar)

Stand: 2026-10-16, autonome Nacht-Session. Wo ich ohne dich weitermachen konnte,
habe ich so entschieden. Nichts davon ist in Stein gemeißelt — jede Zeile hier
hat eine Gegen-Frage in `FRAGEN.md`.

| # | Thema | Entscheidung | Warum | Umkehrbar? |
|---|-------|--------------|-------|-----------|
| 1 | Engine heute Nacht | **Nicht angetastet** (`vcrack.cpp` State/Resume bleibt wie getestet) | Laufende Rechnung darf nach Rebuild weiter resumen | ja |
| 2 | `.vc`-Format | **Text/INI**, abschnittsweise (`[section]` + `key=value`) | lesbar, diff-bar, passt zu list.txt/rules.txt | ja (Spec) |
| 3 | `vcrack.config`-Format | **Text/INI** | wie #2 | ja |
| 4 | Fortschritt in `.vc` | `.vc` **referenziert** separate `*.state`-Datei (altes Format) | Resume-Engine unangetastet | ja |
| 5 | Header-Namensschema | Neues Schema `[name].[ext]_header.hf` nur für **neue** Header; `.tc`/`.hc` weiter unterstützt | bricht laufende Instanz nicht | ja |
| 6 | Dropdown-Auto-Update | Header nur **neu auslesen wenn keine Header-Datei existiert**, sonst nur `.vc` laden | vermeidet unnötige I/O-Last auf große Volumes | ja |
| 7 | Default-Dateiendungen | `.hc`, `.tc` | heutiges Verhalten | ja |
| 8 | Sprachen zum Start | **Deutsch** (Start) + **English** | heutige GUI ist DE | ja |
| 9 | Startsprache | Deutsch, in Einstellungen umschaltbar | heutiger Zustand | ja |
| 10 | Label-Hash | **SHA-256**, 16 Hex-Zeichen gekürzt, über normalisierte Spec-Tabelle | kompakt, reicht für Kompatibilitäts-Check | ja |
| 11 | Wort-Index | **0-basiert**, intern | passt zu Bitmasken | ja |
| 12 | Wort-Limit | **64 aktive Wörter** (Engine-Grenze), Byte-Index-Feld bis 255 vorbereitet | 64-Bit-Masken im Kern | ja (F1) |
| 13 | `used`-Flag-Semantik | gesetzt sobald ein `run` mit dem Wort **gestartet** wurde | konservativ (nie fälschlich löschbar) | ja |
| 14 | `vcrack.config`-Ort | App-/hashcat-Ordner (portabel) | passt zum heutigen Aufbau | ja |
| 15 | Variablen-Namen | exakt nach Issue „Detect all Text" (`[tab]_[type]_##`, Index-Bänder) | Vorgabe der Issue | — |
| 16 | CSV-Trenner | `;`, UTF-8, **kein `;` in Texten** | Vorgabe der Issue | — |

## Was heute Nacht NICHT gebaut wurde (bewusst, siehe FRAGEN.md)
- Umbau der Engine auf `.vc`-basierten State (A1/A2/A3) — wartet auf deine Entscheidung.
- Engine auf >64 Wörter (F1).
- Regel-Baustein-UI (G2) — nur Variablen-Platz vorbereitet.
- `vcrack migrate`-Kommando (A2).
