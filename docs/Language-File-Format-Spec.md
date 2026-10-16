# Language-File Format Specification — `<Language>.lng`

One file per language, holding the translated texts for every UI variable.
Keyed by the same index numbers as `text_variable_specs.csv`.

> Issue reference: "Language File", "Multi Language".

## 1. File name

```
[English Language Name].lng
```

The base name is the **English** name of the language, so files sort and
reference predictably regardless of the active UI language:
```
English.lng
German.lng
French.lng
```

## 2. Encoding & format

- UTF-8, LF line endings.
- Line-oriented. The first three lines are a fixed header, then the entries.

```
[native language name]
[label-hash]
[Translator Credit];[Optional Translator Credit];[Optional Translator Credit] …

[index-number];[translated text]
[index-number];[translated text]
[index-number];[translated text]
…
```

- **Line 1** — the language's name in its own language (e.g. `Deutsch`,
  `Français`). This is what the language picker shows.
- **Line 2** — the label-hash this translation was made against (see
  `Text-Variable-Spec.md` §3). Used to warn when the translation is stale.
- **Line 3** — translator credits, `;`-separated, one or more. May be empty
  (an empty third line) but the line must exist.
- A blank separator line, then the entries.

### Entries

```
[index-number];[translated text]
```

- `index-number` matches the `Index-Number` column in
  `text_variable_specs.csv` (which maps it to a variable name).
- `translated text` — the string in this language. **Never** contains `;`
  (same hard rule as the CSV).
- One entry per line. Order is free, but ascending by index is recommended.
- A missing index falls back to the English text from the CSV. An index not
  present in the CSV is ignored (and reported by the hash check).

## 3. Example (`German.lng`)

```
Deutsch
9f2a1c7b4e6d8a03
vcrack-Team

000;Cockpit
001;Statistik
002;Regeln
003;Wortliste
100;Speichern
101;Neu laden
102;Start
103;Stop
400;Angriff
401;Header auslesen
```

## 4. Compatibility check

On startup the server reads line 2 of the active `.lng` and compares it to
the app's built-in label-hash (also stored in `vcrack.config [app]
label_hash`). If they differ, the UI shows a non-blocking notice that the
translation may be out of date; untranslated or newly-added strings fall
back to the English master so the app stays fully usable.

## 5. Adding a language

1. Copy `English.lng` (or run `tools/labelhash.py --emit-lng German`).
2. Set line 1 to the native name, keep line 2 (the current label-hash),
   set line 3 credits.
3. Translate each entry's text (leave the `index;` prefix).
4. Drop the file next to the app; it appears in the language picker
   automatically.
