# Text-Variable Specification — `text_variable_specs.csv` + Label-Hash

Every piece of visible text in the GUI is replaced by a **variable**. The
master table of variables lives in `text_variable_specs.csv` with English as
the native language. Translations live in `<Language>.lng` files keyed by the
same index numbers (see `Language-File-Format-Spec.md`).

> Issue reference: "Detect all Text in the App", "Text-Hash".

## 1. Variable name format

```
[tab]_[type]_##
```

`##` = a two-digit running number, `00`..`99`, counted **per unique
`tab_type` combination**.

### Reserved global bands (`tab = app`)

| Purpose | tab | type | var numbering | Index-Number band |
|---------|-----|------|---------------|-------------------|
| Tab names (e.g. "Cockpit") | `app` | `tab` | 00–99 | **000–099** |
| Button texts (Save, Edit, Remove, Delete, Open, Start, Stop, OK, Reload, Cancel, Close, Exit, …) | `app` | `cmd` | 00–99 | **100–199** |
| Command output text | `app` | `cxt` | 00–99 | **200–299** |
| Rule bricks (word-rule words) | `app` | `rlz` | 00–99 | **300–399** |

Buttons reuse existing `app_cmd_##` variables wherever the same label
appears again.

### Regular text (everything else)

- `tab` ∈ { `app` (global, not inside a tab), `cockpit`, `statistic`,
  `rules`, `words`, `options`, … and any new tab }.
- `type` ∈
  - `lbl` — labels / titles
  - `txt` — normal text below titles
  - `cxt` — command/protocol output text
  - `cap` — control captions (option, input, or other control texts)
  - `drp` — dropdown texts
- var numbering runs 00–99 **per unique `tab_type`** combination.
- Index-Number band: **400 and above**.

## 2. CSV file

```
text_variable_specs.csv
```

- `.csv`, **`;`-separated**, UTF-8.
- **Never** use `;` inside any text (hard rule — it is the field separator).
- First line is the header meta line:
  ```
  [Version]-[Date]-[Label-Hash]
  ```
  e.g. `1-2026-10-16-9f2a1c7b4e6d8a03`
- Then a column header line, then one row per variable:
  ```
  Index-Number;Variable-Name;Text
  000;app_tab_00;Cockpit
  001;app_tab_01;Statistics
  100;app_cmd_00;Save
  101;app_cmd_01;Reload
  400;cockpit_lbl_00;Attack
  …
  ```

Index numbers must fall inside the band for their `tab_type` (table above).
Within a band they are assigned in reading order (top-to-bottom, as the
control appears in the UI).

## 3. Label-Hash

A single short hash over **all** variable rows, used to check whether a
`.lng` file still matches the app version (issue "Text-Hash").

### Algorithm (decision #10)

1. Take every data row (skip the meta line and the column header).
2. For each row build the string `index|variable|english_text` (the native
   English text from the CSV, **not** the translation).
3. Sort these strings by index ascending (stable).
4. Join them with `\n`.
5. `SHA-256` the UTF-8 bytes.
6. The **label-hash** = the first **16 hex characters** of the digest.

Hashing the English master (not the translations) means the hash identifies
the *set and meaning of UI strings*, independent of language. A `.lng`
carries the label-hash it was translated against; if it differs from the
running app's hash, the translation is flagged "may be out of date".

### Where the label-hash is stored

- `text_variable_specs.csv` — in the meta line (authoritative source).
- every `<Language>.lng` — line 2.
- `vcrack.config` — `[app] label_hash` (the running app's current hash).

### Generator

`tools/labelhash.py` computes the hash from the CSV and can rewrite the CSV
meta line and stamp the `.lng` files. Run it whenever UI text changes:
```
python tools/labelhash.py --csv text_variable_specs.csv --check
python tools/labelhash.py --csv text_variable_specs.csv --write   # stamp meta + lng
```

## 4. Runtime use

- The app ships `text_variable_specs.csv` and the `.lng` files.
- `index.html` carries no literal user-facing text; each element uses
  `data-i18n="<variable>"` (and `data-i18n-ph` for input placeholders,
  `data-i18n-title` for tooltips).
- On load the server returns the active language's strings (keyed by
  variable name, resolved from index via the CSV); the UI fills every
  `data-i18n*` node.
- English is always available even without a `.lng` because the CSV *is* the
  English source.
