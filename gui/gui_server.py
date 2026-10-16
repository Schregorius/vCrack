#!/usr/bin/env python3
# vcrack GUI - lokaler Steuer-Server (nur Python-Standardbibliothek).
# Startet/stoppt vcrack.exe, liest Fortschritt/Status, bearbeitet Wortliste & Regeln.
# Laeuft im hashcat-Ordner (wo vcrack.exe + list.txt + rules.txt + Fvc_header.tc liegen).

import os, sys, re, json, time, threading, subprocess, http.server, socketserver

HERE   = os.path.dirname(os.path.abspath(__file__))
WORK   = os.environ.get("VCRACK_DIR", HERE)      # Arbeitsordner (hashcat-Ordner)
PORT   = int(os.environ.get("VCRACK_PORT", "8765"))
IS_WIN = (os.name == "nt")
VCRACK = os.path.join(WORK, "vcrack.exe") if IS_WIN else os.path.join(WORK, "vcrack")
STATE  = os.path.join(WORK, "vcrack_state.txt")
LOG    = os.path.join(WORK, "gui_run.log")

run_lock   = threading.Lock()
run_proc   = None
run_start  = 0.0
run_cfg    = {}

# ---------------------------------------------------------------- Helfer
def count_done():
    try:
        with open(STATE, "r", encoding="utf-8", errors="ignore") as f:
            return sum(1 for ln in f if ln.startswith("DONE|"))
    except FileNotFoundError:
        return 0

def read_text(name):
    try:
        with open(os.path.join(WORK, name), "r", encoding="utf-8", errors="ignore") as f:
            return f.read()
    except FileNotFoundError:
        return ""

def write_text(name, content):
    with open(os.path.join(WORK, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(content)

# ---------------------------------------------------------------- Ressourcen / i18n
# text_variable_specs.csv und die <Language>.lng liegen im App-Ordner (WORK);
# fuer die Entwicklung auch neben dem Server (HERE) oder im Repo-Root darueber.
RES_DIRS = [WORK, HERE, os.path.dirname(HERE)]

def find_res(name):
    for d in RES_DIRS:
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return p
    return None

def read_ini(path):
    """Minimaler INI-Reader: {section: {key: value}} + {section:'__list__': [lines]}.
    Fuer vcrack.config und .vc (gleiches Format). Listen-Sektionen (words/rules/extensions)
    liefern zusaetzlich die Rohzeilen unter dem Schluessel '*'. Unbekannte Zeilen bleiben erhalten."""
    data = {}
    cur = None
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            for raw in f:
                ln = raw.rstrip("\n").rstrip("\r")
                s = ln.strip()
                if not s or s.startswith("#"):
                    continue
                if s.startswith("[") and s.endswith("]"):
                    cur = s[1:-1].strip().lower()
                    data.setdefault(cur, {}).setdefault("*", [])
                    continue
                if cur is None:
                    continue
                data[cur]["*"].append(s)
                if "=" in ln:
                    k, v = ln.split("=", 1)
                    data[cur][k.strip().lower()] = v.strip()
    except FileNotFoundError:
        pass
    return data

def load_csv_strings():
    """-> (label_hash, idx2var, var2eng). Englischer Master aus text_variable_specs.csv."""
    p = find_res("text_variable_specs.csv")
    label_hash, idx2var, var2eng = "", {}, {}
    if not p:
        return label_hash, idx2var, var2eng
    with open(p, "r", encoding="utf-8", errors="ignore") as f:
        lines = [ln.rstrip("\n").rstrip("\r") for ln in f]
    if lines:
        label_hash = lines[0].split("-")[-1].strip()
    for ln in lines[2:]:
        if not ln.strip():
            continue
        parts = ln.split(";")
        if len(parts) < 3:
            continue
        idx, var, text = parts[0].strip(), parts[1].strip(), ";".join(parts[2:])
        idx2var[idx] = var
        var2eng[var] = text
    return label_hash, idx2var, var2eng

def parse_lng(path):
    """-> (native_name, lng_hash, {idx: text})."""
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = [ln.rstrip("\n").rstrip("\r") for ln in f]
    native = lines[0].strip() if len(lines) > 0 else ""
    lhash  = lines[1].strip() if len(lines) > 1 else ""
    entries = {}
    for ln in lines[3:]:
        if ";" in ln:
            idx, text = ln.split(";", 1)
            if idx.strip():
                entries[idx.strip()] = text
    return native, lhash, entries

def list_languages():
    """Alle gefundenen <Language>.lng (einmalig je Basisname)."""
    seen, out = set(), []
    for d in RES_DIRS:
        try:
            for fn in sorted(os.listdir(d)):
                if fn.lower().endswith(".lng"):
                    name = fn[:-4]
                    if name in seen:
                        continue
                    seen.add(name)
                    native, lhash, _ = parse_lng(os.path.join(d, fn))
                    out.append({"name": name, "native": native, "hash": lhash})
        except Exception:
            pass
    return out

def active_language(default=None):
    """Aus vcrack.config [ui] language, sonst Default (German falls vorhanden, sonst English)."""
    cfgp = find_res("vcrack.config")
    if cfgp:
        cfg = read_ini(cfgp)
        lang = cfg.get("ui", {}).get("language")
        if lang:
            return lang
    if default:
        return default
    langs = {l["name"] for l in list_languages()}
    return "German" if "German" in langs else ("English" if langs else "English")

def i18n_payload(lang=None):
    label_hash, idx2var, var2eng = load_csv_strings()
    if not lang:
        lang = active_language()
    strings = dict(var2eng)            # englischer Fallback zuerst
    native, lng_hash = lang, ""
    lp = find_res(lang + ".lng")
    if lp:
        native, lng_hash, entries = parse_lng(lp)
        for idx, text in entries.items():
            var = idx2var.get(idx)
            if var:
                strings[var] = text
    hash_ok = (lng_hash == label_hash) if lng_hash else None
    return {"lang": lang, "native": native, "label_hash": label_hash,
            "lng_hash": lng_hash, "hash_ok": hash_ok, "strings": strings}

# ---------------------------------------------------------------- vcrack.config / .vc
import datetime
def _utcnow():
    return datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")

DEFAULT_EXT = [".hc", ".tc"]

def write_ini(path, sections):
    """sections: list of (name, dict) oder (name, list-of-rawlines)."""
    out = []
    for name, body in sections:
        out.append("[%s]" % name)
        if isinstance(body, dict):
            for k, v in body.items():
                if k == "*":
                    continue
                out.append("%s = %s" % (k, v))
        else:
            for ln in body:
                out.append(str(ln))
        out.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out).rstrip() + "\n")

def config_load():
    p = find_res("vcrack.config")
    ini = read_ini(p) if p else {}
    def g(sec, key, d): return ini.get(sec, {}).get(key, d)
    exts = ini.get("extensions", {}).get("*", []) if ini.get("extensions") else []
    return {
        "app": {"version": g("app", "version", "1"),
                "app_version": g("app", "app_version", "1.1.0"),
                "app_path": g("app", "app_path", WORK),
                "label_hash": g("app", "label_hash", "")},
        "ui": {"language": g("ui", "language", "German"),
               "theme": g("ui", "theme", "auto")},
        "extensions": [e for e in exts if "=" not in e] or list(DEFAULT_EXT),
        "header": {"default_bytes": g("header", "default_bytes", "524288")},
        "defaults": {"min_len": g("defaults", "min_len", "7"),
                     "max_len": g("defaults", "max_len", "10"),
                     "hashes": g("defaults", "hashes", ""),
                     "sizes": g("defaults", "sizes", ""),
                     "device": g("defaults", "device", "gpu"),
                     "checkpoint": g("defaults", "checkpoint", "300"),
                     "workload": g("defaults", "workload", "3"),
                     "temp_abort": g("defaults", "temp_abort", "90")},
        "stats": {"global_runtime_sec": g("stats", "global_runtime_sec", "0"),
                  "global_hash_count": g("stats", "global_hash_count", "0"),
                  "targets_seen": g("stats", "targets_seen", "0"),
                  "last_used": g("stats", "last_used", "")},
    }

def config_save(patch):
    cfg = config_load()
    for sec, vals in (patch or {}).items():
        if sec == "extensions" and isinstance(vals, list):
            cfg["extensions"] = [str(x) for x in vals]
        elif sec in cfg and isinstance(cfg[sec], dict) and isinstance(vals, dict):
            cfg[sec].update({k: str(v) for k, v in vals.items()})
    lh, _, _ = load_csv_strings()
    cfg["app"]["label_hash"] = lh
    cfg["stats"]["last_used"] = _utcnow()
    write_ini(os.path.join(WORK, "vcrack.config"), [
        ("app", cfg["app"]), ("ui", cfg["ui"]),
        ("extensions", cfg["extensions"]),
        ("header", cfg["header"]), ("defaults", cfg["defaults"]), ("stats", cfg["stats"]),
    ])
    return cfg

VC_SECTION_ORDER = ["meta", "target", "header", "settings", "words", "rules", "stats", "progress"]
VC_LIST_SECTIONS = {"words", "rules", "progress"}   # progress: rohe DONE/PART-Zeilen erhalten

def _write_vc(path, ini):
    sections = []
    for name in VC_SECTION_ORDER:
        if name not in ini:
            continue
        if name in VC_LIST_SECTIONS:
            sections.append((name, ini[name].get("*", [])))
        else:
            sections.append((name, {k: v for k, v in ini[name].items() if k != "*"}))
    # keep any unknown sections verbatim at the end
    for name, body in ini.items():
        if name not in VC_SECTION_ORDER:
            sections.append((name, body.get("*", []) if "*" in body else body))
    write_ini(path, sections)

def vc_find_for(header_name):
    """Finde die .vc-Datei, deren [header] file == header_name ist."""
    for d in RES_DIRS:
        try:
            for fn in sorted(os.listdir(d)):
                if fn.lower().endswith(".vc"):
                    full = os.path.join(d, fn)
                    ini = read_ini(full)
                    if ini.get("header", {}).get("file", "") == header_name:
                        return full, ini
        except Exception:
            pass
    return None, None

def vc_load_settings(header_name):
    p, ini = vc_find_for(header_name)
    if not ini:
        return {"exists": False, "settings": config_load()["defaults"]}
    s = {k: v for k, v in ini.get("settings", {}).items() if k != "*"}
    return {"exists": True, "file": os.path.basename(p), "settings": s}

def vc_save_settings(header_name, settings):
    p, ini = vc_find_for(header_name)
    if not ini:
        return {"error": "no .vc for this target yet"}
    ini.setdefault("settings", {})
    for k, v in (settings or {}).items():
        ini["settings"][k] = str(v)
    _write_vc(p, ini)
    return {"ok": True, "file": os.path.basename(p)}

# ---------------------------------------------------------------- Wortliste (Byte-Index + used)
# Strukturierte Wortliste in list.meta (Zeilen "index ; used ; text", identisch zum
# .vc [words]-Format, spaeter 1:1 uebernehmbar). list.txt wird daraus fuer die Engine
# regeneriert (aktive Woerter in Index-Reihenfolge). Index-/used-Logik aus VC-Spec §4.
WORD_ENGINE_LIMIT = 64      # 64-Bit-Masken im Generator-Kern
WORD_MAX_INDEX    = 255     # Byte-Index laut Issue (0..254)
WL_META = "list.meta"

def wl_load():
    p = os.path.join(WORK, WL_META)
    words = []
    if os.path.isfile(p):
        with open(p, "r", encoding="utf-8", errors="ignore") as f:
            for ln in f:
                s = ln.strip()
                if not s or s.startswith("#"):
                    continue
                parts = s.split(";")
                if len(parts) < 3:
                    continue
                try: idx = int(parts[0].strip())
                except ValueError: continue
                used = parts[1].strip() in ("1", "true", "True")
                text = ";".join(parts[2:]).strip()
                words.append({"index": idx, "used": used, "text": text})
    else:
        # Bootstrap aus list.txt (ein Wort pro Zeile) -> Indizes in Reihenfolge, used=0
        lp = os.path.join(WORK, "list.txt")
        if os.path.isfile(lp):
            i = 0
            with open(lp, "r", encoding="utf-8", errors="ignore") as f:
                for ln in f:
                    t = ln.strip()
                    if t:
                        words.append({"index": i, "used": False, "text": t}); i += 1
    words.sort(key=lambda w: w["index"])
    return words

def wl_save(words):
    words = sorted(words, key=lambda w: w["index"])
    with open(os.path.join(WORK, WL_META), "w", encoding="utf-8", newline="\n") as f:
        for w in words:
            f.write("%d ; %d ; %s\n" % (w["index"], 1 if w["used"] else 0, w["text"]))
    # list.txt fuer die Engine: aktive (nicht-leere) Woerter in Index-Reihenfolge
    with open(os.path.join(WORK, "list.txt"), "w", encoding="utf-8", newline="\n") as f:
        for w in words:
            if w["text"]:
                f.write(w["text"] + "\n")
    return words

def wl_active(words):
    return [w for w in words if w["text"]]

def wl_payload(words=None, error=None):
    if words is None:
        words = wl_load()
    return {"words": words, "active": len(wl_active(words)),
            "engine_limit": WORD_ENGINE_LIMIT, "max_index": WORD_MAX_INDEX,
            "error": error}

def wl_add(text):
    text = (text or "").strip()
    words = wl_load()
    if not text:
        return wl_payload(words, error="empty")
    if any(w["text"].lower() == text.lower() for w in words):
        return wl_payload(words, error="double")
    if len(wl_active(words)) >= WORD_ENGINE_LIMIT:
        return wl_payload(words, error="engine")
    nextidx = (max((w["index"] for w in words), default=-1) + 1)
    if nextidx > WORD_MAX_INDEX:
        return wl_payload(words, error="max")
    words.append({"index": nextidx, "used": False, "text": text})
    return wl_payload(wl_save(words))

def wl_edit(index, text):
    text = (text or "").strip()
    words = wl_load()
    if not text:
        return wl_payload(words, error="empty")
    if any(w["text"].lower() == text.lower() and w["index"] != index for w in words):
        return wl_payload(words, error="double")
    for w in words:
        if w["index"] == index:
            w["text"] = text
            return wl_payload(wl_save(words))
    return wl_payload(words, error="notfound")

def wl_remove(index):
    words = wl_load()
    tgt = next((w for w in words if w["index"] == index), None)
    if tgt is None:
        return wl_payload(words, error="notfound")
    if tgt["used"]:
        return wl_payload(words, error="used")   # benutzte Indizes bleiben gepinnt
    higher = [w for w in words if w["index"] > index]
    words = [w for w in words if w["index"] != index]
    if higher and all(not w["used"] for w in higher):
        for w in higher:
            w["index"] -= 1                       # Luecke schliessen (alle hoeheren unbenutzt)
    return wl_payload(wl_save(words))

def wl_mark_all_used():
    """Beim Start eines Runs: alle aktiven Woerter auf used=1."""
    words = wl_load()
    changed = False
    for w in words:
        if w["text"] and not w["used"]:
            w["used"] = True; changed = True
    if changed:
        wl_save(words)

# ---------------------------------------------------------------- Ziel-Automatik (.vc anlegen/laden)
def _vc_name(header_name, size):
    base = re.sub(r'[\\/:*?"<>|]', "_", header_name)
    return "%s-%d.vc" % (base, size)

def ensure_vc(header_name):
    """Beim Waehlen eines Ziels: vorhandene .vc laden, sonst additiv neu anlegen.
    Beruehrt weder Header-Datei noch vcrack_state (Engine bleibt unangetastet)."""
    if not header_name:
        return {"error": "no target"}
    p, ini = vc_find_for(header_name)
    if ini:
        return {"created": False, "file": os.path.basename(p),
                "settings": {k: v for k, v in ini.get("settings", {}).items() if k != "*"}}
    hp = None
    for d in RES_DIRS:
        cand = os.path.join(d, header_name)
        if os.path.isfile(cand):
            hp = cand; break
    size = os.path.getsize(hp) if hp else 0
    sha = ""
    if hp:
        import hashlib
        h = hashlib.sha256()
        with open(hp, "rb") as f:
            for chunk in iter(lambda: f.read(65536), b""):
                h.update(chunk)
        sha = h.hexdigest()
    cfg = config_load(); d = cfg["defaults"]
    name = _vc_name(header_name, size)
    words = ["%d ; %d ; %s" % (w["index"], 1 if w["used"] else 0, w["text"]) for w in wl_load()]
    rules = []
    rp = os.path.join(WORK, "rules.txt")
    if os.path.isfile(rp):
        for ln in open(rp, "r", encoding="utf-8", errors="ignore"):
            s = ln.strip()
            if s and not s.startswith("#"):
                rules.append(s)
    ini = {
        "meta": {"version": "1", "created": _utcnow(),
                 "app_version": cfg["app"]["app_version"], "label_hash": cfg["app"]["label_hash"]},
        "target": {"name": header_name, "size": size},
        "header": {"file": header_name, "bytes": cfg["header"]["default_bytes"], "sha256": sha},
        "settings": {"min_len": d["min_len"], "max_len": d["max_len"], "hashes": d["hashes"],
                     "sizes": d["sizes"], "device": d["device"], "checkpoint": d["checkpoint"],
                     "workload": d["workload"], "temp_abort": d["temp_abort"]},
        "words": {"*": words},
        "rules": {"*": rules},
        "stats": {"runtime_sec": "0", "hashes_tested": "0", "found": "0"},
        "progress": {"*": []},   # leer; die Engine haengt DONE/PART direkt ans Dateiende an
    }
    _write_vc(os.path.join(WORK, name), ini)
    return {"created": True, "file": name, "settings": ini["settings"]}

def list_targets():
    """Moegliche VeraCrypt-Header im Arbeitsordner (fuer den Ziel-Selektor).
    Endungen kommen aus vcrack.config [extensions] (+ Heuristik 'vc_header')."""
    try:
        exts = [e.lower() if e.startswith(".") else "." + e.lower()
                for e in config_load().get("extensions", DEFAULT_EXT)]
    except Exception:
        exts = list(DEFAULT_EXT)
    out = []
    try:
        for fn in sorted(os.listdir(WORK)):
            low = fn.lower()
            full = os.path.join(WORK, fn)
            if not os.path.isfile(full):
                continue
            if low.endswith(".vc") or low.endswith(".state") or low.endswith(".lng"):
                continue
            if any(low.endswith(e) for e in exts) or "vc_header" in low:
                out.append({"name": fn, "size": os.path.getsize(full)})
    except Exception:
        pass
    return out

def extract_header(src, nbytes, target):
    """vcrack header ... --json aufrufen."""
    if not nbytes:
        try: nbytes = int(config_load()["header"]["default_bytes"])
        except Exception: nbytes = 524288
    args = ["header", "--src", str(src)]
    if nbytes:  args += ["--bytes", str(int(nbytes))]
    if target:  args += ["--target", str(target)]
    args += ["--json"]
    return run_json(args, timeout=120)

def run_json(args, timeout=120):
    """vcrack mit --json aufrufen und die JSON-Zeile aus stdout holen."""
    try:
        p = subprocess.run([VCRACK] + args, cwd=WORK, capture_output=True,
                           text=True, timeout=timeout)
    except Exception as e:
        return {"error": str(e)}
    out = (p.stdout or "").strip().splitlines()
    for ln in reversed(out):
        ln = ln.strip()
        if ln.startswith("{"):
            try: return json.loads(ln)
            except Exception: pass
    return {"error": "keine JSON-Ausgabe", "stderr": (p.stderr or "")[-400:]}

SPEED_RE = re.compile(r"Speed\.#\d+\.+:\s*([\d.]+)\s*([kMGT]?)H/s", re.I)
TEMP_RE  = re.compile(r"Temp:\s*(\d+)c", re.I)
UTIL_RE  = re.compile(r"Util:\s*(\d+)%", re.I)
FAN_RE   = re.compile(r"Fan:\s*(\d+)%", re.I)
CORE_RE  = re.compile(r"Core:\s*(\d+)\s*MHz", re.I)
PROG_RE  = re.compile(r"Progress\.+:\s*(\d+)/(\d+)")
BLOCK_RE = re.compile(r"\[Block (\d+)/(\d+)\]\s*(.+)")
TOTAL_RE = re.compile(r"=\s*(\d+)\s*Bloecke")
MULT = {"": 1, "k": 1e3, "M": 1e6, "G": 1e9, "T": 1e12}

def parse_log():
    """Letzte relevante Werte aus gui_run.log ziehen."""
    d = {"speed": 0, "temp": 0, "util": 0, "fan": 0, "core": 0,
         "block_idx": 0, "block_total": 0, "block_desc": "",
         "prog_cur": 0, "prog_max": 0, "found": False, "password": "", "tail": ""}
    try:
        with open(LOG, "r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
    except FileNotFoundError:
        return d
    tail = lines[-400:]
    for ln in tail:
        m = SPEED_RE.search(ln);  d["speed"] = float(m.group(1)) * MULT.get(m.group(2), 1) if m else d["speed"]
        m = TEMP_RE.search(ln);   d["temp"] = int(m.group(1)) if m else d["temp"]
        m = UTIL_RE.search(ln);   d["util"] = int(m.group(1)) if m else d["util"]
        m = FAN_RE.search(ln);    d["fan"]  = int(m.group(1)) if m else d["fan"]
        m = CORE_RE.search(ln);   d["core"] = int(m.group(1)) if m else d["core"]
        m = PROG_RE.search(ln);
        if m: d["prog_cur"], d["prog_max"] = int(m.group(1)), int(m.group(2))
        m = BLOCK_RE.search(ln)
        if m: d["block_idx"], d["block_total"], d["block_desc"] = int(m.group(1)), int(m.group(2)), m.group(3).strip()
        m = TOTAL_RE.search(ln)
        if m: d["block_total"] = int(m.group(1))
        if "[+] ERFOLG" in ln: d["found"] = True
    if d["found"]:
        try: d["password"] = read_text("geknackt.txt").strip()
        except Exception: pass
    # Nur den NEUESTEN hashcat-Statusblock zeigen (von letztem "Session" bis "Hardware.Mon"),
    # davor die aktuelle [Block ..]-Zeile. Sonst stuenden mehrere Bloecke gleichzeitig im Log.
    last_sess = None; last_block = ""
    for i, ln in enumerate(tail):
        if ln.lstrip().startswith("[Block "): last_block = ln.strip()
        if ln.startswith("Session"): last_sess = i
    if last_sess is not None:
        seg = []
        for ln in tail[last_sess:]:
            seg.append(ln)
            if ln.lstrip().startswith("Hardware.Mon"): break
        d["tail"] = (last_block + "\n\n" if last_block else "") + "".join(seg)
    else:
        d["tail"] = "".join(tail[-20:])
    return d

def status():
    global run_proc
    running = False
    with run_lock:
        if run_proc is not None:
            running = (run_proc.poll() is None)
    lg = parse_log()
    st = {
        "running": running,
        "elapsed": int(time.time() - run_start) if run_start else 0,
        "done_blocks": count_done(),
        "cfg": run_cfg,
    }
    st.update(lg)
    return st

def start_run(cfg):
    global run_proc, run_start, run_cfg
    with run_lock:
        if run_proc is not None and run_proc.poll() is None:
            return {"error": "laeuft bereits"}
        args = [VCRACK, "run"]
        if cfg.get("hash"):  args += ["--hash", str(cfg["hash"])]
        if cfg.get("size"):  args += ["--size", str(cfg["size"])]
        if cfg.get("min"):   args += ["--min",  str(cfg["min"])]
        if cfg.get("max"):   args += ["--max",  str(cfg["max"])]
        if cfg.get("device"):args += ["--device", str(cfg["device"])]
        if cfg.get("target"):args += ["--target", str(cfg["target"])]
        if cfg.get("checkpoint") is not None: args += ["--checkpoint", str(cfg["checkpoint"])]
        try: wl_mark_all_used()          # alle aktiven Woerter als "in einem Run benutzt" markieren
        except Exception: pass
        logf = open(LOG, "wb")
        kw = {}
        if IS_WIN:
            kw["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
        else:
            kw["start_new_session"] = True
        run_proc = subprocess.Popen(args, cwd=WORK, stdout=logf,
                                    stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, **kw)
        run_start = time.time(); run_cfg = cfg
        return {"ok": True, "pid": run_proc.pid}

def stop_run():
    global run_proc
    with run_lock:
        if run_proc is None or run_proc.poll() is not None:
            return {"ok": True, "note": "nichts aktiv"}
        pid = run_proc.pid
        try:
            if IS_WIN:
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(pid)],
                               capture_output=True)
            else:
                import signal
                os.killpg(os.getpgid(pid), signal.SIGTERM)
        except Exception as e:
            return {"error": str(e)}
        return {"ok": True}

# ---------------------------------------------------------------- HTTP
class H(http.server.BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)): body = json.dumps(body)
        if isinstance(body, str): body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype + ("; charset=utf-8" if "json" in ctype or "html" in ctype or "text" in ctype else ""))
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a): pass  # ruhig

    def do_GET(self):
        p = self.path.split("?")[0]
        if p == "/" or p == "/index.html":
            try:
                with open(os.path.join(HERE, "index.html"), "rb") as f:
                    self._send(200, f.read(), "text/html")
            except FileNotFoundError:
                self._send(404, {"error": "index.html fehlt"})
        elif p == "/api/status":
            self._send(200, status())
        elif p == "/api/words":
            self._send(200, read_text("list.txt"), "text/plain")
        elif p == "/api/rules":
            self._send(200, read_text("rules.txt"), "text/plain")
        elif p == "/api/targets":
            self._send(200, {"targets": list_targets()})
        elif p == "/api/languages":
            act = active_language()
            langs = list_languages()
            for l in langs:
                l["active"] = (l["name"] == act)
            self._send(200, {"languages": langs, "active": act})
        elif p == "/api/i18n":
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            lang = (q.get("lang", [None])[0]) or None
            self._send(200, i18n_payload(lang))
        elif p == "/api/config":
            self._send(200, config_load())
        elif p == "/api/vc":
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            tgt = (q.get("target", [""])[0]) or ""
            self._send(200, vc_load_settings(tgt))
        elif p == "/api/wordlist":
            self._send(200, wl_payload())
        elif p == "/api/target/select":
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            self._send(200, ensure_vc((q.get("name", [""])[0]) or ""))
        else:
            self._send(404, {"error": "unbekannt"})

    def do_POST(self):
        p = self.path.split("?")[0]
        n = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(n) if n else b""
        if p == "/api/words":
            write_text("list.txt", raw.decode("utf-8")); self._send(200, {"ok": True}); return
        if p == "/api/rules":
            write_text("rules.txt", raw.decode("utf-8")); self._send(200, {"ok": True}); return
        try: body = json.loads(raw.decode("utf-8")) if raw else {}
        except Exception: body = {}
        if p == "/api/config":
            self._send(200, config_save(body)); return
        if p == "/api/vc":
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            tgt = (q.get("target", [""])[0]) or body.get("target", "")
            self._send(200, vc_save_settings(tgt, body.get("settings", {}))); return
        if p == "/api/wordlist":
            op = body.get("op", "list")
            if op == "add":      self._send(200, wl_add(body.get("text", "")))
            elif op == "edit":   self._send(200, wl_edit(int(body.get("index", -1)), body.get("text", "")))
            elif op == "remove": self._send(200, wl_remove(int(body.get("index", -1))))
            else:                self._send(200, wl_payload())
            return
        if p == "/api/run":
            act = body.get("action")
            if act == "start": self._send(200, start_run(body.get("cfg", {})))
            elif act == "stop": self._send(200, stop_run())
            else: self._send(400, {"error": "action?"})
        elif p == "/api/header":
            self._send(200, extract_header(body.get("src", ""), body.get("bytes", 524288), body.get("target", "")))
        elif p == "/api/keyspace":
            args = ["sample", "--min", str(body.get("min", 7)), "--max", str(body.get("max", 10)),
                    "--samples", str(body.get("samples", 300000)), "--json"]
            self._send(200, run_json(args))
        elif p == "/api/estimate":
            args = ["estimate", "--min", str(body.get("min", 7)), "--max", str(body.get("max", 10)), "--json"]
            if body.get("hash"): args += ["--hash", str(body["hash"])]
            if body.get("size"): args += ["--size", str(body["size"])]
            if body.get("device"): args += ["--device", str(body["device"])]
            self._send(200, run_json(args, timeout=600))
        elif p == "/api/bench":
            args = ["bench"]
            if body.get("device"): args += ["--device", str(body["device"])]
            if body.get("rebench"): args += ["--rebench"]
            try:
                subprocess.run([VCRACK] + args, cwd=WORK, capture_output=True, timeout=600)
                self._send(200, {"ok": True})
            except Exception as e:
                self._send(200, {"error": str(e)})
        else:
            self._send(404, {"error": "unbekannt"})

class TS(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True

if __name__ == "__main__":
    os.chdir(WORK)
    srv = TS(("127.0.0.1", PORT), H)
    print(f"vcrack GUI laeuft auf http://127.0.0.1:{PORT}  (Ordner: {WORK})")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
