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

### 1. Schreibfläche (InkPages-Kern)
- Seiten in einem DirectManipulation-Viewport: Finger-Pan mit Schwung, Pinch-Zoom, **Präzisions-Touchpad**
  (`DM_POINTERHITTEST` → `SetContact`), Mausrad; Stift und Maus verschieben nie.
- Ein Ink-Presenter pro Seite oder einer über alle Seiten – per Messung entscheiden (eigene Stroke-Container pro Seite
  wie im .xopp-Format vs. Aufwand); Seiten nur nahe am Sichtbereich mit Tinte (wie heute: lazy).
- Papier (Raster) mit Direct2D; Seitenhintergründe als Ebenen unter der Tinte.
- Stift-Cursor in Werkzeugfarbe/-breite (vorbereitete Cursor-Ressourcen je Farbe × Breite × DPI-Stufe).
- Messen am Gerät: Latenz, Handballen, Stift + Finger.

### 2. Werkzeuge (InkPages)
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

### 5. Umstieg
- Paritätsliste erfüllt, Sandbox-Tests grün, Gerätetest durch den Nutzer → Installer der C++-App, C#-App entfernen.

## Paritätsliste (Auszug, wird vor Phase 5 vervollständigt)
Alle Punkte der heutigen CLAUDE.md „Open work“/„Decided“: Werkzeuge, Auswahl, Radierer, Formen, Lineal, Undo,
Seitenverwaltung, PDF/Bilder/Texte, Speichern-Frage, Wiederherstellung, Explorer inkl. Verwerfen, Git-Sync, Aufgaben,
PDF-Export, iPad-Import, Einfügen aus PDF/.xopp, Meldungen, Fehlerprotokoll.

## Offene Fragen (für den Nutzer bzw. per Messung)
1. **PDF-Export ohne PDFsharp:** Kandidat ganz nativ: Direct2D-Druck (`ID2D1PrintControl`) auf „Microsoft Print to
   PDF“, PDF-Seiten über `IPdfRendererNative::RenderPageToDeviceContext` als Vektoren – muss per Test-App geprüft werden
   (bleibt der Text Text, Größe, Annotationen). Alternative: eine kleine PDF-Bibliothek (z. B. qpdf, Apache-2.0).
2. **Ein Presenter pro Seite oder pro Dokument** (Phase 1, Messung).
3. **Lizenz der neuen C++-Quellen** (wie bisher); upstream-Xournal++-Code (GPL) wird nicht übernommen, sondern unser
   C#-Code portiert – es sei denn, der Nutzer möchte das anders.
