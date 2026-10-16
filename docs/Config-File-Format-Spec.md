# Config-File Format Specification — `vcrack.config`

Global application settings and statistics for vcrack — everything that is
**not** tied to one specific target. Per-target data lives in the `.vc` file
instead (see `VC-File-Format-Spec.md`).

> Issue reference: "neu: vcrack.config". This spec is the deliverable
> requested there ("Erstelle dazu eine Config-Datei-Format-Spezifikation.md").

## 1. File name & location

```
vcrack.config
```

Lives in the application / hashcat folder next to the engine (portable,
decision #14). One file per installation.

## 2. Encoding & format

Same INI-like text format as the `.vc` file: UTF-8, LF, `[section]` +
`key = value`, `#` comments, case-insensitive keys. List values use one
item per line inside a dedicated section.

## 3. Sections

### `[app]`
```
version     = 1              # vcrack.config format version
app_version = 1.1.0          # vcrack version last run
app_path    = C:\hashcat-7.1.2
label_hash  = 9f2a1c…        # label-hash of the running UI (see Text-Variable spec)
```

### `[ui]`
```
language   = Deutsch         # active UI language = a <Language>.lng base name
theme      = auto            # auto | light | dark
```

### `[extensions]`
The file extensions shown in the target dropdown (editable in Settings via
an add/edit/remove list). One extension per line, leading dot optional:
```
[extensions]
.hc
.tc
```

### `[header]`
Global default header extraction size (the control moved out of the Cockpit
tab into Settings):
```
default_bytes = 524288       # 512 KB, covers hidden-volume headers too
```

### `[defaults]`
Default values copied into every **newly created** `.vc` `[settings]`
block. Changing them here does not touch existing `.vc` files.
```
min_len    = 7
max_len    = 10
hashes     =                 # empty = all
sizes      =                 # empty = all
device     = gpu
checkpoint = 300
workload   = 3
temp_abort = 90
```

### `[stats]`
Global, cross-target counters (informational):
```
global_runtime_sec = 259200   # total GPU seconds across all targets
global_hash_count  = 987654321
targets_seen       = 3
last_used          = 2026-10-16T01:00:00Z
```

## 4. Creation & defaults

If `vcrack.config` is missing, the server creates it with the values above
(language `Deutsch`, extensions `.hc`/`.tc`, device `gpu`, etc.). Unknown
keys are preserved on rewrite so a newer app version can add keys without an
older one dropping them.

## 5. Label-hash handling

`[app] label_hash` is written from the app's built-in UI label-hash on each
start. The server compares it against the active `.lng`'s hash; a mismatch
is surfaced to the user as "translation may be out of date" (see
`Language-File-Format-Spec.md` and `Text-Variable-Spec.md`).
