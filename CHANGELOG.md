# Changelog

All notable changes to vcrack are documented here.

---

## [1.1.0] — 2026-10-16

### Deutsch

GUI und Datenhaltung ausgebaut. **Der Generator-/Angriffs-Kern (`vcrack.cpp`) ist
unverändert** — bestehende Läufe und das Resume-Verhalten bleiben identisch, ein
`.exe`-Rebuild ist für die neuen Funktionen nicht nötig (alles Python/HTML/CSV).

**Neu**

- **Mehrsprachige GUI.** Sämtliche Oberflächentexte liegen als Variablen in
  `text_variable_specs.csv`; Übersetzungen in `<Sprache>.lng` (mitgeliefert:
  Deutsch und English). Umschaltbar im Kopf und in den Einstellungen. Ein
  **Label-Hash** (SHA-256) erkennt automatisch, wenn eine Übersetzung nicht mehr
  zur App-Version passt.
- **Einstellungen-Tab**, zweigeteilt:
  - *Global* → `vcrack.config`: Sprache, Design, erkannte Dateiendungen
    (Add/Edit/Remove-Liste), Standard-Header-Bytes, Standardwerte für neue Ziele.
  - *Zieldatei-spezifisch* → `.vc`-Datei des gewählten Ziels.
- **Zieldatei-`.vc`.** Pro Ziel eine Datei (`<Name>-<Bytes>.vc`) mit Header-Pfad +
  SHA-256, Einstellungen, Wortliste, Regeln, Statistik und Fortschritts-Verweis.
  Wird beim Wählen eines Ziels automatisch angelegt oder geladen.
- **Wortliste als Liste** statt Textfeld: Add/Edit/Remove mit Eingabemaske. Jedes
  Wort erhält einen **Byte-Index** und ein **„benutzt"-Flag** (wird beim Run-Start
  gesetzt). Lösch- und Index-Verschiebe-Regeln laufen serverseitig; benutzte
  Indizes bleiben gepinnt.
- **Header-Automatik.** Das Ziel-Dropdown lädt/erstellt beim Wählen automatisch die
  `.vc`. Dateiendungen und Header-Bytes sind in die Einstellungen gewandert.

**Grenzen**

- Maximal **255 Wörter** (Byte-Index 0–254). Der Generator-Kern nutzt
  64-Bit-Masken, daher aktuell höchstens **64 aktive** Wörter — die GUI weist
  darauf hin.

**Dokumentation**

- Neuer Ordner `docs/` mit den Format-Spezifikationen (`.vc`, `vcrack.config`,
  `.lng`, Text-Variablen/Label-Hash) und dem Architektur-Plan.

**Noch offen (für den nächsten Release)**

- Ob der Fortschritt physisch *in* die `.vc` wandert oder sie ihn nur referenziert
  (aktuell: referenziert, damit die Resume-Engine unangetastet bleibt).
- Engine-Ausbau auf mehr als 64 Wörter (128/256-Bit-Masken), falls gewünscht.

### English

Expanded the GUI and the data model. **The generator / attack core
(`vcrack.cpp`) is unchanged** — existing runs and resume behaviour are identical,
and no `.exe` rebuild is needed for the new features (all Python/HTML/CSV).

**Added**

- **Multi-language GUI.** Every UI string lives as a variable in
  `text_variable_specs.csv`; translations in `<Language>.lng` (shipped: German and
  English). Switchable in the header and in Settings. A **label-hash** (SHA-256)
  automatically detects when a translation no longer matches the app version.
- **Settings tab**, split in two:
  - *Global* → `vcrack.config`: language, theme, recognised file extensions
    (add/edit/remove list), default header bytes, defaults for new targets.
  - *Per-target* → the selected target's `.vc` file.
- **Per-target `.vc`.** One file per target (`<name>-<bytes>.vc`) holding the
  header path + SHA-256, settings, word list, rules, stats and a progress pointer.
  Created or loaded automatically when a target is selected.
- **Word list as a list** instead of a text box: add/edit/remove with an input
  mask. Each word gets a **byte index** and a **"used" flag** (set at run start).
  Deletion and index-shift rules run server-side; used indices stay pinned.
- **Header automation.** Selecting a target in the dropdown loads/creates its
  `.vc` automatically. File extensions and header bytes moved into Settings.

**Limits**

- At most **255 words** (byte index 0–254). The generator core uses 64-bit masks,
  so currently at most **64 active** words are allowed — the GUI warns about this.

**Documentation**

- New `docs/` folder with the format specs (`.vc`, `vcrack.config`, `.lng`, text
  variables/label-hash) and the architecture plan.

**Still open (for a future release)**

- Whether progress physically moves *into* the `.vc` or is only referenced by it
  (currently: referenced, so the resume engine stays untouched).
- Widening the engine beyond 64 words (128/256-bit masks), if needed.

---

## [1.0.2] and earlier

Rule-based VeraCrypt password recovery: native C++ permutation generator streamed
into hashcat, runtime rule system (`rules.txt`), all-in-one `estimate`, header
extraction, resume/checkpoints with a per-candidate cover filter that skips
already-tested combinations across word and rule changes. See the README for
details.
