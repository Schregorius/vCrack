# Fragen — alle beantwortet (2026-10-16)

Alle offenen Punkte wurden mit dem User durchgegangen; die verbindlichen
Entscheidungen stehen in `ENTSCHEIDUNGEN.md`. Daraus ergibt sich diese
Umsetzungsliste für die nächsten Schritte.

## Umsetzungsliste (noch zu bauen)

1. **A1 — Fortschritt in die `.vc` einbetten.** Statt eines Verweises auf eine
   separate `*.state`-Datei wandert der Fortschritt physisch in die `.vc`
   (`[progress]`-Block). Beim Lauf: Export der State-Zeilen aus der `.vc` in das
   Format, das die Engine erwartet, und Re-Import danach. Engine + Server.
   → `VC-File-Format-Spec.md` §Progress entsprechend aktualisiert.

2. **E1 — `used`-Flag erst bei Block-Ende.** Die heute Nacht gebaute Markierung
   „bei Run-Start" wird ersetzt: ein Wort gilt erst als benutzt, wenn ein Block,
   der es enthielt, abgeschlossen ist. Ableitung aus den `DONE`-Zeilen des
   Fortschritts.

3. **Arbeitsordner `E:\vcrack`.** Künftig Standard-Arbeitsordner. Der GUI-Starter
   ist bereits pfadrelativ (`%~dp0`) und funktioniert dort ohne Änderung. Nur ein
   Beispielpfad in der Config-Spec war hartkodiert → angepasst. Geräteseitige
   Feinheiten erledige ich, sobald die Verbindung zum Rechner steht.

4. **G2 — Regel-Baustein-UI** als eigenes Issue einplanen (Variablen-Platz
   `app_rlz_##` ist vorbereitet). Vorerst bleibt der Regeln-Tab ein Textfeld.

## Bereits erledigt (Nacht-Session 2026-10-16)
Mehrsprachigkeit, Einstellungen-Tab, `vcrack.config`, `.vc`-Scaffolding,
Wortlisten-Item-UI, Header-Automatik, Specs. Siehe `CHANGELOG.md` (1.1.0).
