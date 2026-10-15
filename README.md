# vcrack 1.0.0

**English** | [Deutsch](README-DE.md)

**Rule-based VeraCrypt password recovery.** vcrack generates sentence permutations
from a word list according to configurable rules and streams them straight into
[`hashcat`](https://hashcat.net). It is meant for the case where you forgot your
**own** VeraCrypt password but still remember the *building blocks* (words, names,
numbers) — just not their exact order.

> ⚠️ For recovering your own data only. Using it against volumes you do not own,
> without permission, is illegal.

The idea: instead of blindly trying every character combination (hopeless), only
the **meaningful space** of known words + rules is generated. That shrinks the
search space from "infinite" to "feasible".

---

## Features

- **Native C++ generator** (no runtime required), streams candidates directly into hashcat (`-a 0` stdin mode) — nothing is written to disk.
- **Rule system** (`rules.txt`): length, position rules, forbidden adjacencies, exclusions, etc. — loaded at runtime, **no recompile** when rules change.
- **Keyspace estimation via Monte-Carlo** with an error margin (±%), in under a second, incl. a DOS-style dashboard and per-length distribution.
- **Runtime estimation** per hashcat mode (measures H/s, computes ETA).
- **Resume & checkpoints**: progress is saved per block; within a block at least every few minutes (`--checkpoint`). Aborting never loses more than that interval.
- **Smart resume on changes**: add a word → only the *new* combinations are tested. Remove a word / tighten a rule → skipped. Nothing is computed twice.
- **Search-hash cache**: `count`, `sample` and `estimate` share results as long as words/rules/lengths are unchanged.
- **Safety check**: only starts if hashcat actually sees a GPU device, and marks a block as done **only** after a genuine (exhausted) hashcat run — no fake progress when the GPU is missing.
- **Graphical interface** (optional): local Win11-style Fluent web UI, opens in a standalone window.

---

## Requirements

- Windows with **hashcat 7.x** (`hashcat.exe` in the same folder) and a working GPU (CUDA/OpenCL).
- To build: **MSVC** (Visual Studio 2022/2026, "x64 Native Tools").
- For the GUI: **Python 3** (standard library only).

## Putting files in place

vcrack runs **in the hashcat folder** (next to `hashcat.exe`). Place there:

```
hashcat.exe
vcrack.cpp          <- source
build.bat           <- build script
rules.txt           <- your rules (template: rules.example.txt)
list.txt            <- your word list (one word per line)
<volume>.tc         <- VeraCrypt header of the target
vcrack-gui.bat      <- GUI launcher
gui\                <- GUI server + page
```

**Extracting the header** (the first 512 KB of the VeraCrypt volume are enough;
only the first 512 bytes are checked). See the hashcat docs on the VeraCrypt modes
for details on obtaining the header.

## Build

```
build.bat
```
Produces `vcrack.exe`. (Alternatively, in the "x64 Native Tools Command Prompt":
`cl /nologo /O2 /EHsc /std:c++17 /Fe:vcrack.exe vcrack.cpp`)

---

## Commands

```
vcrack rules       show active rules (from rules.txt)
vcrack sample      quickly estimate the keyspace (Monte-Carlo, +/- margin, dashboard)
vcrack count       count the keyspace exactly (slower, multi-core, cached)
vcrack estimate    keyspace x H/s -> runtime table per mode
vcrack bench       measure and cache H/s per hash
vcrack gen         candidates to stdout (for testing/piping)
vcrack run         attack: modes x length x first-word, with resume
```

### Options

| Option | Meaning |
|---|---|
| `--words F` | word list (default `list.txt`) |
| `--rules F` | rule file (default `rules.txt`) |
| `--target F` | VeraCrypt header (default `Fvc_header.tc`) |
| `--min N` / `--max N` | words per candidate (default from `rules.txt`) |
| `--hash H` | single hash only: `SHA-512`, `Whirlpool`, `Streebog`, `SHA-256` |
| `--size S` | single cipher size only: `512`, `1024`, `1536` (single/2-/3-cipher cascade) |
| `--device D` | `gpu` (default), `cpu`, `both` |
| `--workload N` | hashcat `-w` 1..4 (default 3; higher = more GPU, less desktop) |
| `--checkpoint SEC` | save run progress at least every SEC seconds (default 300, 0 = off) |
| `--temp N` | GPU emergency abort at N °C (default 90) |
| `--samples N` | samples for sample/estimate (default 300000) |
| `--rebench` | re-measure H/s instead of using the cache |
| `--json` | machine-readable output (for the GUI) |

### Typical workflow

```
vcrack rules
vcrack bench
vcrack estimate --max 8      # look at realistic runtimes
vcrack run --hash SHA-512 --size 512 --max 8
```

---

## Rule file (`rules.txt`)

Lines starting with `#` are comments. Rule words are case-insensitive. English
keywords are primary; the former German keywords still work as aliases.

| Rule | Meaning | Alias |
|---|---|---|
| `length A B` | candidate consists of A..B words | `laenge` |
| `frompos N : w1 w2 ...` | these words only from position N | `abpos` |
| `forbid-adjacent L.. -> R..` | forbid a transition (if left L, then not right R) | `nachbarn-verboten` |
| `exclusive w1 w2 ...` | at most one of these words per candidate | `exklusiv` |
| `not-at-end N : w1 ...` | not within the last N positions | `nicht-am-ende` |
| `max-one-ever` | special "ever" combination logic | `max-ein-ever` |

A neutral template ships as **`rules.example.txt`**. Copy it to `rules.txt` and
adapt it to your words. Maximum **64 words** in the list (internal bitmasks).

---

## Resume / checkpoints

- Run order: per **mode** → per **length** → per **first word** = one block.
- Each finished block is stored as a `DONE` line in `vcrack_state.txt`.
- Within a block, `--checkpoint` stores the offset as a `PART` line.
- Running `vcrack run` again resumes automatically; finished work is skipped.
- Changing the word list/length: only the **new** part is tested (the delta), never from scratch.
- Changing a rule changes the rule signature → safe recomputation.

State/cache (created automatically, **not** in the repo):
`vcrack_state.txt`, `vcrack_cache.txt`, `vcrack_bench.txt`.

---

## Graphical interface

```
vcrack-gui.bat
```
Starts the local server (`gui\gui_server.py`, Python standard library only) and
opens a **standalone window** (Chrome/Edge in app mode). Tabs: **Cockpit**
(start/stop, live status), **Statistics** (keyspace + runtime), **Rules**,
**Word list**. Keep the console window open — closing it stops the server.

---

## hashcat VeraCrypt modes (reference)

The last digit of the `-m` code = cipher cascade: `…1` = single cipher (default),
`…2` = 2-cipher, `…3` = 3-cipher cascade. vcrack tries them in order of likelihood.

| Hash | 512 | 1024 | 1536 |
|---|---|---|---|
| SHA-512 | 13721 | 13722 | 13723 |
| Whirlpool | 13731 | 13732 | 13733 |
| Streebog | 13771 | 13772 | 13773 |
| SHA-256 | 13751 | 13752 | 13753 |

For VeraCrypt the **hash** (PBKDF2 iterations) determines the speed, not the cipher
cascade — so vcrack measures one H/s per hash and reuses it.

---

## Security / not in the repo

`.gitignore` keeps **sensitive and generated files out**: `list.txt`, `rules.txt`,
the volume header (`*.tc`, `*vc_header*`), `geknackt.txt`, the progress/cache
(`vcrack_*.txt`) and `*.exe`. Never upload your real word list or header — they
reveal your password.

## License

MIT — see [LICENSE](LICENSE).
