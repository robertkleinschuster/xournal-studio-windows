# Umzug von InkPages und Xournal Studio nach C++/Win32

Stand 2026-11-19. Entscheidung des Nutzers: **nativ, schlank, Win32 – aber die moderne Variante davon.** Die Win32-Demo
(`D:\src\win32-ink-demo`) ist am Gerät „zufriedenstellend“: Windows Ink über den Win32-Presenter
(`IInkDesktopHost`/`IInkPresenterDesktop`) in eigener DirectComposition, Finger-Pan/Pinch mit DirectManipulation,
farbiger Stift-Cursor über `CoreInkIndependentInputSource.PointerCursor`.

Der Mathe Trainer zieht **später** um; sein InkPages-Submodul bleibt bis dahin auf der letzten WinUI-Version
(Tag `winui-final`). Seine Vorüberlegungen: `D:\src\MatheTrainer\docs\CPP-UMZUG.md`.

## Leitlinien

- **Technik:** C++20, rohes Win32, C++/WinRT für WinRT-APIs (Windows.UI.Input.Inking, InkAnalyzer, Windows.Data.Pdf).
  Steuerelemente aus comctl32 v6 mit Visual Styles (Windows-11-Optik), Menüs aus user32, Dialoge als `TaskDialog`
  bzw. Dialogvorlagen; Zeichnen mit DirectComposition, Direct2D, DirectWrite, WIC. PerMonitorV2. Kein UI-Framework
  obendrauf, kein Mischen (keine XAML-Islands).
- **Schlank:** möglichst nur Windows-Komponenten; Fremdbibliotheken nur wo Windows nichts hat (zlib für gzip, libgit2
  für Git) und dann statisch gelinkt. Ziel: ein kleines Programm, Start in Zehntelsekunden.
- **Bedienung:** Stift bedient alles (Steuerelemente in normaler Maus-Größe, nicht fingergroß); Finger und Touchpad
  verschieben und zoomen nur das Blatt.
- **Gleiches Verhalten wie heute**, OneNote als Vorlage für offene Fragen. Die Regeln der bisherigen CLAUDE.md gelten
  weiter: upstream-.xopp-Format, nie aus Dateien löschen, Speichern nur auf Wunsch + Wiederherstellungskopie, nie
  Ausnahmen bzw. Fehler verschlucken (HRESULT, Typ, Text loggen), kleine Klassen mit einer Aufgabe, getestet.
- **Symbole und Farben (Nutzer, 2026-11-19):** Befehle mit Segoe Fluent Icons (Undo/Redo = die offiziellen E7A7/E7A6),
  wo keine Glyphe passt eigene Pfad-Icons im selben Stil; Dateien und Ordner im Explorer-Baum mit der Systembildliste der
  Shell; Meldungen mit den Info-/Warn-/Fehlersymbolen des Systems (`TaskDialog`, `SIID_*`). **Vorerst nur hell:** Win32
  hat für comctl32-Steuerelemente keinen dokumentierten Dark Mode (Microsoft Learn „Support Dark and Light themes in
  Win32 apps“ dokumentiert nur Erkennen und die dunkle Titelleiste); halb dunkel wäre Frankenstein, die undokumentierten
  uxtheme-Wege ein Hack. Wieder anschauen, sobald Microsoft ihn dokumentiert.
- **Die heutige C#/WinUI-App bleibt installiert und nutzbar**, bis die C++-App alles kann (Paritätsliste unten).

## Repositories

- **InkPages:** die WinUI-Version wird mit `winui-final` getaggt (der Mathe Trainer pinnt darauf). Danach beginnt im
  selben Repository die C++-Bibliothek (statische Bibliothek + Demo-App + Tests), die WinUI-Quellen werden entfernt
  (über den Tag erreichbar). Name bleibt InkPages.
- **Xournal Studio:** im selben Repository ein neuer Ordner `native/` mit der C++-App; die C#-App bleibt daneben, bis
  sie ersetzt ist, dann wird sie entfernt.
- Build: CMake (mit Visual Studio 2026), `build.ps1`; Tests mit CTest; Sandbox-Tests weiter über TestKit/Inj.

## Phasen

### 0. Vorbereitung – erledigt 2026-11-19
- `winui-final` in InkPages getaggt (c0c2e57), Mathe Trainer darauf gepinnt.
- C++-Projektgerüst für InkPages (1817e3f): CMake, statische Bibliothek mit `inkpages::Log`, Demo-Fenster (comctl32 v6,
  PerMonitorV2), Unit-Tests mit GoogleTest/CTest (Microsofts CppUnitTest fand im CMake-Build keine Tests), `build.ps1`,
  CI; Sandbox-Starttest grün.
- Xournals Submodul `external/InkPages` bleibt auf dem WinUI-Stand, solange die C#-App lebt (nicht auf C++-Stände
  anheben); die C++-App in `native/` bindet InkPages später direkt ein.

### 1. Schreibfläche (InkPages-Kern) – erledigt 2026-11-19 (InkPages 5e933ec, aee6153, daae96a)
- `WritingSurface` (Kindfenster): DirectManipulation-Pan mit Schwung, Pinch 0,2–6×, Präzisions-Touchpad
  (`DM_POINTERHITTEST`), Mausrad/Strg+Rad; Papier mit Direct2D; lazy Seiten (`load`/`store`, `Hold`); Stift-Cursor
  aus App-Ressourcen. **Ein Presenter für das ganze Dokument** (gemessen: ~5 ms, Speicher unabhängig von der
  Seitenzahl; pro Seite: Absturz beim Setzen des Cursors während der Aktivierung eines weiteren Presenters). Striche
  gehören zur Seite ihres ersten Punkts. Sandbox: Tinte nach Zoom/Pan/Seite 2 auf 0,3 DIP genau. Offen am Gerät:
  Touchpad, Latenz, Handballen. Offen für Phase 2: Striche über die Seitengrenze (abschneiden/teilen), Auswahl vs.
  `Selected`-Flag, der Cursor wird 300 ms nach dem Erzeugen gesetzt (Zeitpolster gegen den Absturz – Ursache klären).

Ursprüngliche Planung:
- Seiten in einem DirectManipulation-Viewport: Finger-Pan mit Schwung, Pinch-Zoom, **Präzisions-Touchpad**
  (`DM_POINTERHITTEST` → `SetContact`), Mausrad; Stift und Maus verschieben nie.
- Ein Ink-Presenter pro Seite oder einer über alle Seiten – per Messung entscheiden (eigene Stroke-Container pro Seite
  wie im .xopp-Format vs. Aufwand); Seiten nur nahe am Sichtbereich mit Tinte (wie heute: lazy).
- Papier (Raster) mit Direct2D; Seitenhintergründe als Ebenen unter der Tinte.
- Stift-Cursor in Werkzeugfarbe/-breite (vorbereitete Cursor-Ressourcen je Farbe × Breite × DPI-Stufe).
- Messen am Gerät: Latenz, Handballen, Stift + Finger.

### 2. Werkzeuge (InkPages)
**2b/2c erledigt 2026-11-19 (InkPages c88e462 … b68b242):** Bibliothek modular (`toolbar → surface → tools → ink →
render → model`, `base`), Auswahl Lasso/Rechteck mit Stifttaste, Bild-Erweiterungspunkt (`PictureHost`), exaktes
Verschieben/Skalieren, über Seiten mit Auto-Scroll, Befehle inkl. eigenem Zwischenablage-Format; Formen, Formerkennung
(`InkAnalyzer`), Windows-Lineal/Winkelmesser; Striche an der Seitengrenze geteilt; Strich-Schlüssel (verlustfreies
Speichern) und Seiten-Unterlage (`drawUnderlay`) für die App. 45 Unit-Tests, Sandbox `SelectionTest`/`ToolsTest` grün.
Offen am Gerät: Touchpad, Lineal per Finger, Latenz, Handballen.

**2a erledigt 2026-11-19 (InkPages feb8dda, cccb628, 040af1b):** comctl32-Werkzeugleiste (Rückgängig/Wiederholen,
Stift- und Textmarker-Vorlagen mit Farb-/Breiten-Menü, Radierer + „Ganze Striche radieren“ mit Kreisgröße),
Radierer ganz/exakt (StrokeEraser portiert), Radierer-Ende, eine Rückgängig-Historie über alle Seiten, Stift-Cursor je
Vorlage. Das 300-ms-Zeitpolster ist weg: der Cursor wird beim ersten Zeiger-Ereignis des Presenters gesetzt (erst dann
ist seine Eingabe aktiv). 29 Unit-Tests, Sandbox `ToolsTest.ps1` grün. Offen: 2b Auswahl, 2c Formen/Erkennung/Lineal.

- Werkzeugleiste mit comctl32-Toolbar: Stift-/Textmarker-Presets mit Farb-/Breiten-Menü, Radierer (+ exakt/ganze
  Striche), Auswählen, Formen, Lineal (`InkPresenterRuler`), Rückgängig/Wiederholen, Auswahlbefehle.
- Auswahl (Lasso/Rechteck über `UnprocessedInput`, Verschieben/Skalieren exakt, Stifttaste = Lasso, Antippen daneben
  hebt auf), Teil-Radierer, Formerkennung (`InkAnalyzer`), Undo-Stapel, seitenübergreifendes Verschieben mit
  Auto-Scroll.
- Was heute in `Model/` liegt (StrokeEraser, ShapeGeometry, Tap, PenWidths, …), wird mit seinen Tests portiert.

### 3. Xournal-Grundgerüst
- Hauptfenster mit Menü, Werkzeugleiste, Seitenleiste (Explorer-Baum, Aufgaben, Seiten-Miniaturen), Meldungen
  (Toast-artig), Dialoge (`TaskDialog`, Dateiauswahl über `IFileOpenDialog`/`IFileSaveDialog`).
- .xopp lesen/schreiben (XmlLite + zlib), Modell und Unit-Tests aus `XournalStudio.Tests` portiert, gleiche
  Beispieldateien.

### 4. Funktionen
- PDF-Hintergründe (Windows.Data.Pdf, `IPdfRendererNative` auf eigenem Direct2D/D3D-Gerät), Bilder, Texte,
  gezeichnete Striche, alle Hintergründe.
- Speichern/Wiederherstellung, Explorer-Befehle, Git-Sync (libgit2), Aufgaben (todo.txt), Seiten einfügen/verschieben/
  löschen, Seiten aus PDF/Dokument, Zwischenablage.
- PDF-Export: zu klären (siehe offene Fragen).
- iPad-Import zuletzt (plist + PencilKit-Protobuf).

### 4b. Neue Funktionen nach der Parität (Nutzer, 2026-11-19 – nach unseren Bedienkonzepten, nicht wie Xournal++)
- **Text-Werkzeug:** Werkzeug „Text“; Stift-Tipp öffnet ein Textfeld, Tastatur tippt, Tipp daneben beendet; Tipp auf
  einen Text bearbeitet ihn; mit Auswählen wie Striche/Bilder auswählen, verschieben, skalieren; Schrift/Größe/Farbe im
  Menü am Werkzeug; gespeichert als upstream-`<text>`.
- **Suche (Strg+F):** Suchfeld oben in der Leiste; PDF-Text, Texte der Datei und Handschrift (`InkAnalyzer` im
  Hintergrund, wie OneNote); Treffer auf den Seiten markiert, Enter/F3 weiter; Trefferliste im Seitenleisten-Reiter „Suche“.
- **PDF-Sprungmarken:** Inhaltsverzeichnis des PDFs als Baum im Reiter „Gliederung“, Tipp springt; Links im PDF per
  **Fingertipp oder Mausklick** (Finger und Maus schreiben nie – Nutzer, 2026-11-19), Webadressen öffnen den Browser.
- **Die Maus schreibt nie** (Nutzer, 2026-11-19): nur der Stift schreibt. Maus-Ziehen wählt aus (wie Auswählen),
  verschiebt/skaliert die Auswahl, Klick daneben hebt sie auf, Klick auf einen Link folgt ihm.
- **Platz einfügen:** vorerst nicht (Nutzer, 2026-11-19).
- Vorab gemessen (Probe-Agent, D:\src\feature-probes): PDF-Text mit Positionen (Windows.Data.Pdf hat keinen Text),
  Texteingabe auf der Tintenfläche (fensterloses RichEdit vs. Kind-Fenster).

### 5. Umstieg
- Paritätsliste erfüllt, Sandbox-Tests grün, Gerätetest durch den Nutzer → Installer der C++-App, C#-App entfernen.

## Paritätsliste (Auszug, wird vor Phase 5 vervollständigt)
Die C++-App ersetzt die C#-App, wenn alles Folgende funktioniert (Sandbox-Tests + Gerätetest des Nutzers). Abgehakt
wird hier, mit Commit.

**Schreiben (InkPages)**
- [ ] Stift-/Textmarker-Vorlagen mit Farbe (Xournal-Palette) und Breite (Xournal-Stufen), gespeichert; Stift-Cursor
- [ ] Radierer ganz/exakt (Kreis, Radius), Radierer-Ende/-Taste mit jedem Werkzeug
- [ ] Auswahl Lasso/Rechteck, Stifttaste = Lasso, Verschieben/Skalieren exakt, Antippen daneben hebt auf, über Seiten
      verschieben mit Auto-Scroll, Ausschneiden/Kopieren/Duplizieren/Löschen/Alles auswählen
- [ ] Rechteck auf PDF-Seite kopiert den sichtbaren Bereich (PDF + Tinte + Bilder) als Bild
- [ ] Formen (Linie, Pfeil, Rechteck, Ellipse), Formerkennung, Lineal
- [ ] Rückgängig/Wiederholen über alle Seiten (auch Bild-Änderungen, Seitenwechsel beim Verschieben)
- [ ] Finger/Touchpad/Mausrad: Pan mit Schwung, Zoom; Seitenbreite; aktuelle Seite markiert

**Dokument (.xopp, upstream-Format)**
- [ ] Laden/Speichern ohne Verlust (unbekannte Elemente/Attribute bleiben), `<datei>~` dann ersetzen
- [ ] Alle Hintergründe (Farbe, Lineaturen mit config, PDF, Bild absolut/attach/clone), Bilder, Texte/Links,
      gefüllte/gestrichelte Striche angezeigt
- [ ] Speichern nur auf Wunsch, Frage beim Verlassen (Speichern/Nicht speichern/Abbrechen), Speichern unter mit
      Anhängen, Standardname `%F-Notiz-%H-%M` bzw. nach PDF
- [ ] Wiederherstellungskopie (5 s nach Änderung, bei Fokusverlust; Angebot beim Start), Neustart nach Absturz
- [ ] PDF öffnen als neues Dokument; PDF-Hintergrund aus dem Speicher (Datei frei)

**Seiten**
- [ ] Seiten einfügen (leer, aus PDF, aus .xopp – mit Miniaturen-Dialog, vor/nach), verschieben, löschen (mit Frage)
- [ ] Seitenleiste mit Miniaturen (zwei Ebenen, nur geänderte neu), Tippen springt

**Seitenleiste / Projekt**
- [ ] Explorer-Baum (alle Dateien, natürliche Sortierung, Git-Farben inkl. Ordner), Kontextmenü: Öffnen, Neues
      Dokument/Ordner, Umbenennen, Löschen (Papierkorb), Änderungen verwerfen, Im Explorer anzeigen, Als Pfad kopieren;
      F2/Entf/Strg+Umschalt+C; folgt Änderungen auf der Platte
- [ ] Git-Synchronisieren (libgit2, Anmeldedaten des Credential Managers), Sperre während Sync, Konfliktbehandlung
- [ ] Aufgaben (todo.txt): anzeigen, hinzufügen, abhaken, Seitenlink

**Import/Export**
- [ ] PDF-Export (Hintergrund-PDF als Vektoren, Annotationen eingezeichnet, Tinte pro Segment)
- [ ] iPad-Import (PencilKit-Striche, Bilder, Texte)
- [ ] Zwischenablage: eigenes Format + Bild für andere Apps, Bilder/Tinte anderer Apps einfügen

**App**
- [ ] Menü (Datei, Bearbeiten, Seite, Ansicht) mit Tastenkürzeln, Werkzeugleiste mit App-Knöpfen (Einfügen, Seite
      einfügen/löschen), Seitenleiste ein/aus (Strg+B), Toast-Meldungen, Fehlerprotokoll `errors.log`
- [ ] Einstellungen (Werkzeugwahl, Projektordner, Fensterzustand, Positionen pro Dokument)
- [ ] Installation per `install.ps1` (eine kleine Exe), Startmenü, Dateizuordnung wie heute
- [ ] Sandbox-Tests für alle Bereiche; Gerätetest durch den Nutzer

## Offene Fragen (für den Nutzer bzw. per Messung)
1. **PDF-Export ohne PDFsharp – entschieden 2026-11-19: qpdf.** Test (D:\src\pdf-export-probe): der reine Windows-Weg
   (Direct2D-Druck → „Microsoft Print to PDF“) läuft ohne Dialog, Tinte bleibt Vektor, aber eingebettete Teil-Schriften
   der Hintergrund-PDFs (praktisch alle echten PDFs, auch LaTeX-Skripte) werden zu Konturen (nicht durchsuchbar, größer),
   undurchsichtige Bilder zu JPEG, und der Windows-Benutzername steht als Autor im PDF. Der Nutzer wählte qpdf
   (Apache-2.0, statisch): Seiten des Hintergrund-PDFs exakt übernehmen (Annotationen eingezeichnet), Tinte/Lineaturen/
   Bilder/Texte darüber – wie heute mit PDFsharp. Das Export-Modul entsteht zuerst im Probe-Ordner.
2. **Ein Presenter pro Seite oder pro Dokument** (Phase 1, Messung).
3. **Lizenz der neuen C++-Quellen** (wie bisher); upstream-Xournal++-Code (GPL) wird nicht übernommen, sondern unser
   C#-Code portiert – es sei denn, der Nutzer möchte das anders.
