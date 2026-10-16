# VC-File Format Specification

Per-target data file for vcrack. One `.vc` file holds everything tied to a
single encrypted target: where its header is, the per-target settings, the
word list, the sentence/word rules, statistics, and its attack
progress (embedded).

> Issue reference: "neu: .vc datei". This spec is the deliverable requested
> there ("Erstelle dazu eine VC-Datei-Format-Spezifikation.md").

## 1. File name

```
[TargetFileName]-[TargetSizeInBytes].vc
```

- `TargetFileName` = the encrypted target's file name **including** its
  extension, with any character illegal in a file name replaced by `_`.
- `TargetSizeInBytes` = exact size of the target file in bytes.

The byte count is part of the name so two different targets that happen to
share a name do not collide.

Example: target `meine_videos.hc` of 4 294 967 296 bytes →
`meine_videos.hc-4294967296.vc`.

## 2. Encoding & general format

- UTF-8, LF line endings.
- INI-like: `[section]` headers, then `key = value` lines.
- `#` at the start of a line is a comment. Blank lines are ignored.
- Keys are case-insensitive; values keep their case.
- List values: one item per line in a dedicated section (see `[words]`,
  `[rules]`), not comma-joined, so items may contain spaces.

Rationale for text over binary: diff-able, hand-inspectable, matches the
existing `list.txt` / `rules.txt` style (decision #2 in `ENTSCHEIDUNGEN.md`).

## 3. Sections

### `[meta]`
```
version      = 1            # .vc format version
created      = 2026-10-16T01:30:00Z
app_version  = 1.1.0        # vcrack version that created it
label_hash   = 9f2a…        # UI label-hash at creation (informational)
```

### `[target]`
```
name   = meine_videos.hc    # original target file name
size   = 4294967296         # bytes (matches the file name)
path   = E:\meine_videos.hc # last known full path to the source (optional)
```

### `[header]`
```
file   = meine_videos.hc_header.hf   # extracted header file (see §6)
bytes  = 524288                      # how many bytes were extracted
sha256 = …                           # of the header file, to detect changes
```

### `[settings]`  (per-target, see Config spec §defaults for the globals they copy)
```
min_len     = 7
max_len     = 10
hashes      = SHA-512,Whirlpool,Streebog,SHA-256   # empty = all
sizes       = 512,1024,1536                          # empty = all
device      = gpu            # gpu | cpu | both
checkpoint  = 300            # seconds, 0 = off
workload    = 3              # hashcat -w 1..4
temp_abort  = 90             # °C
```

### `[words]`
One word per line. Three fields, `;`-separated (never put `;` in a word):
```
index ; used ; text
```
- `index` — the word's byte index, 0..254 (0-based, decision #11). Stable
  for the life of the file; see §4 for the reindex rules.
- `used` — `0` or `1`; the invisible "was used in a run" flag.
- `text` — the word itself.

Example:
```
[words]
0 ; 1 ; Patricia
1 ; 1 ; Love
2 ; 0 ; Forever
```

> Engine limit: the generator uses 64-bit masks → **at most 64 active
> words** today. The index field allows 0..254 for forward-compatibility,
> but the app must refuse a 65th active word until the engine is widened
> (issue "Wortliste" asks for 255 — open question F1).

### `[rules]`
The rule lines exactly as they appear in `rules.txt` (same grammar, English
keywords primary). One rule per line:
```
[rules]
length 7 10
frompos 4 : 4 !
forbid-adjacent Patricia -> Will Miss Always Me
exclusive Without WithOut
not-at-end 3 : Patricia Love Fuck
max-one-ever
```

### `[stats]`
```
runtime_sec   = 86400        # total GPU seconds spent on this target
hashes_tested = 123456789    # total candidates tried
started       = 2026-10-14T20:00:00Z
last_run      = 2026-10-16T01:00:00Z
found         = 0            # 1 once the password is recovered
```

### `[progress]`
The attack progress is **embedded** in the `.vc` (decision A1). The block holds
the engine's resume lines (`DONE|…` / `PART|…`, see `README` → Resume), one per
line, verbatim:
```
[progress]
DONE|13721|7|Patricia|<rhash>|<wcsv>|<rules>
DONE|13721|8|Love|<rhash>|<wcsv>|<rules>
PART|13721|9|…|<offset>
```
At run time the server exports these lines to the format the engine reads and
re-imports the engine's updated lines back into the `.vc` afterwards, so a single
`.vc` is the complete, portable record of a target (settings + words + rules +
stats + progress). The per-candidate cover-filter logic itself is unchanged.

## 4. Word-list index rules (from issue "Wortliste")

- Indices are assigned **continuously** and never reused casually.
- On **delete** of a word:
  - The deleted index may actually be freed/removed **only if that word was
    never used in a run** (`used = 0`).
  - When a word is deleted, check every word with a **higher** index: if all
    of them are also `used = 0`, shift them down by one (close the gap).
    If any higher word was used, leave the gap (store a tombstone — a line
    with empty text — so later re-add keeps indices stable), because a used
    index must stay pinned to what the engine already tested.
- At the start of every run, **all** words present in the active list are
  set `used = 1` (they participate in the keyspace even if they do not end
  up in a found block).
- When 255 indices are in use, no new word is allowed; the UI shows a
  warning in a rounded-corner box. (This 255 limit is documented in the
  README; the stricter 64 active-word engine limit also applies — F1.)

## 5. Relationship to legacy files

If a target has no `.vc` yet but legacy `list.txt` / `rules.txt` /
`vcrack_state.txt` exist, a one-time `migrate` builds the first `.vc`:
`[words]` from `list.txt` (indices assigned in order, `used=1` for any word
already covered by a DONE line), `[rules]` from `rules.txt`, `[progress]`
seeded with the existing state file's `DONE`/`PART` lines. Nothing is deleted; the legacy files
stay as a backup.

## 6. Header file

Produced by `vcrack header` (unchanged mechanism). New name scheme for
freshly-extracted headers:
```
[TargetFileName].[Extension]_header.hf
```
Existing `.tc` / `Fvc_header.tc` headers remain valid and supported
(decision #5). The `.vc` always records the actual header file name in
`[header] file=`, so either scheme works.
