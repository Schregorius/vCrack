# Offene Fragen (Weiterplanung) — bitte morgen beantworten

Stand: 2026-10-16, autonome Nacht-Session. Ich habe alles abgearbeitet, was ohne
deine Entscheidung ging. Hier die Punkte, bei denen ich **nicht** einfach
entscheiden wollte, weil sie entweder unumkehrbar sind oder deine laufende
Rechnung betreffen. Wo ich provisorisch entschieden habe, steht es in
`ENTSCHEIDUNGEN.md` — das kannst du jederzeit kippen.

---

## A. Resume-Engine & `.vc`-Datei (die wichtigste Frage)

Die Issue „neu: .vc datei" will, dass **Fortschritt + Wortliste + Regeln + Statistik**
alle in die `.vc`-Datei wandern. Genau dieser Fortschritt liegt aber heute in
`vcrack2_state.txt` mit der Cover-Filter-Logik, die wir gerade fertig gebaut und
getestet haben (überspringt bereits Geprüftes auch nach Regeländerungen).

**A1.** Soll der Fortschritt wirklich physisch **in** die `.vc`-Datei wandern, oder
soll die `.vc`-Datei den Fortschritt nur **referenzieren** (Pfad auf eine separate
State-Datei)? 
- *Meine Empfehlung:* `.vc` referenziert eine eigene `*.state`-Datei im selben
  Format wie heute. Grund: die Resume-Logik bleibt unangetastet und bewährt,
  und die `.vc`-Datei bleibt klein/lesbar. Siehe `VC-File-Format-Spec.md` →
  Abschnitt „Progress".
- Wenn du den Fortschritt wirklich eingebettet willst, baue ich einen
  Export/Import, der beim Lauf die State-Zeilen rein-/rausschreibt.

**A2.** Migration: Du hast **jetzt** eine laufende Rechnung mit `vcrack2_state.txt`,
`list.txt`, `rules.txt`. Wenn wir auf `.vc` umstellen — soll ich ein einmaliges
`vcrack migrate`-Kommando bauen, das aus den vorhandenen Dateien die erste
`.vc`-Datei erzeugt, ohne Fortschritt zu verlieren? (Ja = ich baue es, berührt
deine laufende Instanz nicht.)

**A3.** Ich habe die Engine (`vcrack.cpp`) heute Nacht **nicht** umgestellt, damit
deine laufende Rechnung nach einem späteren Rebuild weiter resumen kann. Richtig so?

---

## B. Dateiformate (Text vs. Binär)

**B1.** `.vc`-Datei: Ich habe sie als **lesbares Text-/INI-Format** entworfen (passt zu
`list.txt`/`rules.txt`, leicht zu debuggen, diff-bar). OK? Oder lieber JSON? Oder binär
(kompakter, aber nicht mehr von Hand lesbar)? Spec liegt vor — leicht änderbar.

**B2.** `vcrack.config`: dito, aktuell Text/INI. OK?

---

## C. Header-Automatik & Namensschema

**C1.** Die Issue „Automate Header-Functions" definiert ein **neues** Header-Namensschema:
`[Name der Ziel-Datei].[Endung]_header.hf`. Heute heißt die Datei `Fvc_header.tc`.
Umstellen? Das betrifft deine laufende Instanz (die testet gegen `Fvc_header.tc`).
- *Meine Empfehlung:* neues Schema nur für **neu** ausgelesene Header; bestehende
  `.tc`-Header weiter unterstützen. Dann bricht nichts.

**C2.** „Dropdown-Wahl aktualisiert automatisch den Header" — heißt das: beim Umschalten
des Ziels im Dropdown wird der Header der Quelle **neu ausgelesen** (I/O auf das große
Volume bei jedem Wechsel)? Oder nur die `.vc`-Infos geladen und der Header nur dann
neu ausgelesen, wenn noch keine Header-Datei existiert? Ich habe Letzteres angenommen
(sonst unnötige Last). Bestätige bitte.

**C3.** Dateiendungen-Standardliste fürs Dropdown: aktuell `.tc`, `.hc` + alles mit
`vc_header` im Namen. Welche Endungen sollen standardmäßig drin sein? (z.B. `.hc .tc
.vc-container .mkv` …) Ich habe `.hc .tc` als Default gesetzt.

---

## D. Mehrsprachigkeit

**D1.** Welche Sprachen sollen zum Start mitgeliefert werden? Ich habe **Deutsch**
(native) und **English** vorbereitet. Weitere?

**D2.** Native Standardsprache der Spec-Datei ist laut Issue Englisch — die
ausgelieferte GUI ist aber heute Deutsch. Startsprache der App = Deutsch, umschaltbar?
(So angenommen.)

**D3.** Label-Hash-Verfahren: Ich habe SHA-256 über die normalisierte Variablen-Text-Tabelle
gewählt, auf 16 Hex-Zeichen gekürzt. Reicht dir das als Kompatibilitäts-Prüfung, oder
willst du den vollen Hash?

---

## E. Wortliste (Byte-Index / used-Flag)

**E1.** Der „wurde-in-einem-run-benutzt"-Boolean: Wann genau gilt ein Wort als „benutzt"?
Ich nehme an: **sobald ein `run` gestartet wurde, der dieses Wort in der aktiven Liste
hatte** (unabhängig davon, ob der Block fertig wurde). Alternativ: erst wenn mindestens
ein Block mit dem Wort abgeschlossen ist. Welche Semantik?

**E2.** Index-Vergabe: Indizes 0-basiert oder 1-basiert? Ich habe **0-basiert** gewählt
(passt zu Bitmasken im Code). 255 Wörter max. — heute begrenzt der Code intern auf
**64** Wörter (64-Bit-Bitmasken!). Das ist ein Widerspruch zur Issue (255). Siehe F1.

---

## F. Harte technische Grenze: 64 vs. 255 Wörter

**F1.** Die Engine nutzt **64-Bit-Bitmasken** → **max. 64 Wörter**. Die Issue „Wortliste"
nennt **255** (Byte-Index). Das geht mit dem heutigen Kern nicht zusammen. Optionen:
- (a) Bei 64 Wörtern bleiben, GUI-Limit = 64, Byte-Index trotzdem bis 255 erlauben aber
  Liste auf 64 aktive begrenzen. 
- (b) Engine auf 128/256-Bit-Masken umbauen (`__uint128_t` oder zwei/vier uint64) —
  machbar, aber spürbarer Umbau + etwas langsamer.
- *Meine Empfehlung:* vorerst (a); (b) nur wenn du wirklich mehr als 64 Bausteine
  brauchst. Wie entscheidest du?

---

## G. Sonstiges

**G1.** Wo soll `vcrack.config` liegen — im App-/hashcat-Ordner (neben der exe) oder im
User-Profil (`%APPDATA%\vcrack\`)? Ich habe den App-Ordner gewählt (portabel, passt zum
heutigen Aufbau).

**G2.** Die GUI-Tabs hängen bald von vielen neuen Dateien ab. Soll ich die „Regeln"
weiterhin als rohes Textfeld lassen (Issue „Regeln Tab" ist noch leer/Parent) oder planst
du dort auch eine Baustein-UI? (Es gibt Variablen-Platz `app_rlz_##` für Regel-Bausteine
in der Text-Spec — vorbereitet, aber UI noch nicht gebaut.)
