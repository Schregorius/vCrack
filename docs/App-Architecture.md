# vcrack — Application Architecture

> Parent planning document for the GUI/config rework (GitHub issues
> "App-Architecture", "Regeln Tab", "neu: Einstellung Tab", "neu: vcrack.config",
> "neu: .vc datei", "Automate Header-Functions", "Text-Hash", "Language File",
> "Detect all Text in the App", "Multi Language", "GUI").

This document describes the **target** architecture after the rework. It is the
map; the per-format details live in the sibling specs:

- `Config-File-Format-Spec.md` — the global `vcrack.config`
- `VC-File-Format-Spec.md` — the per-target `<name>-<bytes>.vc`
- `Language-File-Format-Spec.md` — the `<Language>.lng` translation files
- `Text-Variable-Spec.md` — `text_variable_specs.csv` + the label-hash

---

## 1. Components

```
                         ┌─────────────────────────────┐
                         │  Browser UI (index.html)     │
                         │  tabs, data-i18n variables   │
                         └──────────────┬──────────────┘
                                        │ HTTP (localhost)
                         ┌──────────────┴──────────────┐
                         │  gui_server.py               │
                         │  - serves UI + .lng          │
                         │  - reads/writes config & .vc │
                         │  - shells out to the engine  │
                         └──────────────┬──────────────┘
                                        │ argv + --json
                         ┌──────────────┴──────────────┐
                         │  vcrack(.exe)  — C++ engine  │
                         │  generate → hashcat stdin    │
                         │  resume state (cover filter) │
                         └─────────────────────────────┘
```

The **engine** stays the authority for the attack itself and for resume
(the per-candidate cover filter against stored rules — see `README`). The
**server** becomes the authority for *persistent app data*: it owns
`vcrack.config` and every `.vc` file and hands the engine plain
word-list / rule / header inputs the way it does today.

This keeps the proven generate+resume core untouched while all the new
"data model" work (config, per-target files, localization) lands in the
Python layer, which is easy to change and does not need a C++ rebuild.

---

## 2. Data files and ownership

| File | Scope | Owner | Format | Spec |
|------|-------|-------|--------|------|
| `vcrack.config` | global | server | INI-like text | Config spec |
| `<name>-<bytes>.vc` | per target | server | INI-like text | VC spec |
| `<name>.<ext>_header.hf` | per target | engine (`header`) | binary (header bytes) | VC spec §Header |
| `text_variable_specs.csv` | app (ships) | build/dev | CSV `;` | Text-Variable spec |
| `<Language>.lng` | app (ships) | translators | text | Language spec |
| `*.state` (progress) | per target | engine | existing DONE/PART lines | VC spec §Progress |

Legacy files (`list.txt`, `rules.txt`, `vcrack_state.txt`, `Fvc_header.tc`)
remain **readable** so an existing setup keeps working; a one-time
`migrate` turns them into the first `.vc` (planned, see `FRAGEN.md` A2).

---

## 3. Target lifecycle (what happens when you pick a target)

1. User picks a source file (or an entry in the target dropdown).
2. Server computes the `.vc` name: `<basename>-<sizeInBytes>.vc`.
   The byte size disambiguates same-named files.
3. If the `.vc` exists → load everything from it (settings, word list,
   rules, stats, progress pointer, header path).
4. If not → create it from the current global defaults (from
   `vcrack.config`), then extract the header once into
   `<name>.<ext>_header.hf` and record its path.
5. The dropdown change therefore *never* re-reads the header for a target
   that already has a header file (decision #6).

---

## 4. Settings model (two tiers)

- **Global** (lives in `vcrack.config`): language, recognised file
  extensions, app path, global runtime/hash counters, and the **default
  values** copied into every newly-created `.vc`.
- **Per-target** (lives in the `.vc`): word count length, modes + cipher
  sizes to test, compute device, checkpoint seconds, word list, rules,
  stats, progress.

The Settings tab mirrors this split: global block on top, target block
below.

---

## 5. Localization pipeline

```
index.html (data-i18n="cockpit_lbl_00")   ← variables, no literal text
          │
text_variable_specs.csv  (index;variable;English text + label-hash header)
          │  build step hashes the table → LABEL-HASH
          ├──────────────► English.lng  (generated from CSV)
          └──────────────► Deutsch.lng  (translated)
          │
gui_server.py serves the active .lng; UI swaps text by variable at load.
```

The **label-hash** is stored in the CSV header, in every `.lng`, and in
`vcrack.config`. On startup the server compares them: a `.lng` whose hash
differs from the running app's hash is flagged as "may be out of date"
instead of silently showing stale/missing text.

---

## 6. What is intentionally *not* changing yet

- The engine's resume/state machinery (the thing we just finished).
- The 64-word bitmask limit (issue asks for 255 — conflict, see `FRAGEN.md` F1).
- Rule editing stays a raw text area for now (the "Regeln Tab" parent issue
  is still a stub); variable slots `app_rlz_##` are reserved for a future
  rule-brick UI.

See `ENTSCHEIDUNGEN.md` for the full list of provisional calls and
`FRAGEN.md` for everything awaiting your decision.
