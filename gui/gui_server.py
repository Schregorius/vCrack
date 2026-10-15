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

def list_targets():
    """Moegliche VeraCrypt-Header im Arbeitsordner (fuer den Ziel-Selektor)."""
    out = []
    try:
        for fn in sorted(os.listdir(WORK)):
            low = fn.lower()
            full = os.path.join(WORK, fn)
            if not os.path.isfile(full):
                continue
            if low.endswith(".tc") or low.endswith(".hc") or "vc_header" in low:
                out.append({"name": fn, "size": os.path.getsize(full)})
    except Exception:
        pass
    return out

def extract_header(src, nbytes, target):
    """vcrack header ... --json aufrufen."""
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
