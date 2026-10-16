#!/usr/bin/env python3
"""
labelhash.py — compute and stamp the vcrack UI label-hash.

The label-hash is a short fingerprint of the English UI string table
(text_variable_specs.csv). It lets the app tell whether a <Language>.lng
still matches the current set/meaning of UI strings. See
docs/Text-Variable-Spec.md.

Algorithm (must match the server and the spec):
  1. take every data row of the CSV (skip the meta line + column header)
  2. build "index|variable|english_text" for each
  3. sort by index ascending (stable)
  4. join with "\n"
  5. sha256 the UTF-8 bytes
  6. label-hash = first 16 hex chars of the digest

Usage:
  python tools/labelhash.py --csv text_variable_specs.csv            # print hash
  python tools/labelhash.py --csv text_variable_specs.csv --check    # verify meta + *.lng
  python tools/labelhash.py --csv text_variable_specs.csv --write     # stamp meta + *.lng
  python tools/labelhash.py --csv text_variable_specs.csv --emit-lng German
"""
import argparse, hashlib, os, sys, glob

SEP = ";"

def read_csv_rows(path):
    """Return (meta_line, list_of_(index, variable, text))."""
    with open(path, "r", encoding="utf-8", newline="") as f:
        lines = [ln.rstrip("\n").rstrip("\r") for ln in f]
    if not lines:
        raise SystemExit("empty CSV: " + path)
    meta = lines[0]
    rows = []
    for ln in lines[2:]:            # skip meta line and column-header line
        if not ln.strip():
            continue
        parts = ln.split(SEP)
        if len(parts) < 3:
            continue
        idx, var, text = parts[0], parts[1], SEP.join(parts[2:])  # text never has ';' by rule, but be safe
        rows.append((idx.strip(), var.strip(), text))
    return meta, rows

def compute_hash(rows):
    def keyfn(r):
        try: return (0, int(r[0]))
        except ValueError: return (1, r[0])
    ordered = sorted(rows, key=keyfn)
    blob = "\n".join(f"{i}|{v}|{t}" for (i, v, t) in ordered)
    return hashlib.sha256(blob.encode("utf-8")).hexdigest()[:16]

def meta_with_hash(meta, h):
    # meta line format: [Version]-[Date]-[Label-Hash]; replace the last dash-group
    bits = meta.split("-")
    if len(bits) >= 3:
        bits[-1] = h
        return "-".join(bits)
    return meta + "-" + h

def stamp_csv(path, meta, rows, h):
    new_meta = meta_with_hash(meta, h)
    with open(path, "r", encoding="utf-8", newline="") as f:
        lines = f.read().split("\n")
    lines[0] = new_meta
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    return new_meta

def lng_files(csv_path):
    d = os.path.dirname(os.path.abspath(csv_path)) or "."
    return sorted(glob.glob(os.path.join(d, "*.lng")))

def stamp_lng(path, h):
    with open(path, "r", encoding="utf-8", newline="") as f:
        lines = f.read().split("\n")
    while len(lines) < 2:
        lines.append("")
    lines[1] = h                     # line 2 = label-hash
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

def lng_hash(path):
    with open(path, "r", encoding="utf-8", newline="") as f:
        lines = f.read().split("\n")
    return lines[1].strip() if len(lines) >= 2 else ""

def emit_lng(csv_path, lang, h, rows):
    out = os.path.join(os.path.dirname(os.path.abspath(csv_path)) or ".", lang + ".lng")
    if os.path.exists(out):
        raise SystemExit("already exists: " + out)
    body = [lang, h, ""]             # native name (edit), hash, credits (edit)
    body.append("")
    for (i, v, t) in sorted(rows, key=lambda r: int(r[0]) if r[0].isdigit() else 1e9):
        body.append(f"{i}{SEP}{t}")  # English text as a starting point
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(body) + "\n")
    print("wrote", out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="text_variable_specs.csv")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--emit-lng", metavar="LANG")
    a = ap.parse_args()

    meta, rows = read_csv_rows(a.csv)
    h = compute_hash(rows)

    if a.emit_lng:
        emit_lng(a.csv, a.emit_lng, h, rows); return

    if a.write:
        new_meta = stamp_csv(a.csv, meta, rows, h)
        print("CSV meta  ->", new_meta)
        for lf in lng_files(a.csv):
            stamp_lng(lf, h); print("stamped   ->", os.path.basename(lf), h)
        return

    if a.check:
        ok = True
        meta_h = meta.split("-")[-1]
        print(f"computed label-hash: {h}")
        print(f"CSV meta hash      : {meta_h}  {'OK' if meta_h==h else 'MISMATCH'}")
        if meta_h != h: ok = False
        for lf in lng_files(a.csv):
            lh = lng_hash(lf)
            tag = "OK" if lh == h else "MISMATCH (translation may be out of date)"
            print(f"{os.path.basename(lf):20s}: {lh}  {tag}")
            if lh != h: ok = False
        sys.exit(0 if ok else 1)

    print(h)

if __name__ == "__main__":
    main()
