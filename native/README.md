# Xournal Studio for Windows – native (C++/Win32)

The C# WinUI app rebuilt as a native C++20 app (plan: `docs/CPP-UMZUG.md`). Windows APIs first; third-party code only
where Windows has nothing (zlib for gzip; qpdf for writing PDFs; PDFium for PDF text), fetched at a pinned release and
linked statically - except PDFium, a delay-loaded DLL. Licenses: `THIRD-PARTY.md`.

```powershell
.\build.ps1            # configure (Visual Studio's CMake), build Release, run the unit tests (CTest + GoogleTest)
.\build.ps1 -NoTests
```

## Modules

Each module is a static library with its own folder, public headers in `<module>/include/xournal/<module>/` and
internals in `<module>/src/`. The CMake link dependencies are the only allowed direction:

```
base  <-  model  <-  io  <-  services: pdf, pdftext, import (later: git, clipboard, recovery, tasks)  <-  app (UI)
```

| Module | Target | Job | Uses |
|---|---|---|---|
| `base` | `XournalBase` | `Error` (HRESULT + system text, logged when thrown), `Log`, UTF-8/UTF-16 and case-insensitive text, `Paths` (full/relative paths as Windows treats them) | Windows |
| `model` | `XournalModel` | The document in memory: XML tree (`Xml`), `XoppDocument`, `XoppPage` with `PageInk` (stroke elements bound to the ink layer) and `PageImages`, `Strokes` (element <-> ink, `StrokeShape`), `XoppImage`, `XoppText`, `XoppClip`, `Background`, `BackgroundRuling`, references (`src/References.h`), `Snapshot`, `Attachments`, `CopyPages`, `DocumentNames`, `TodoTxt`. No file contents read or written, no UI. | base; crypt32 (base64), Windows' ICU (characters) |
| `io` | `XournalIo` | `XmlText` (XmlLite), `Gzip` (zlib), `Files` (whole-file reads, `<file>~` then replace), `DocumentFile` (load/save/Save As with attachments) | model; xmllite, shlwapi, zlib |
| `pdf` | `XournalPdf` | PDF export (`PdfExport`: export pages in the model's terms - `StrokeShape` parts, `BackgroundRuling`, `XoppText`, picture placement; `DocumentExport`: the app's one call, `PrepareExport` on the ink thread + `ExportDocument` on any thread, never over the background PDF) and `PdfPageAppender` (pages of another PDF appended to the document's PDF, annotations flattened; links to the outside - URI actions - stay, internal GoTo links are dropped as they would point into the source); `PdfAreaRenderer` (areas of pages as PNG with Windows.Data.Pdf on its own Direct3D device, transparent); `PdfNavigation` (from the PDF's bytes: the outline as a tree with named destinations resolved, and a page's links - GoTo or URI - with their areas; places in `PageFrame`'s shown coordinates: crop box turned by /Rotate, as Windows.Data.Pdf shows the page). Background PDF pages are kept exactly (form XObjects; text stays text), texts are embedded TrueType subsets. Draws the model's shapes and rulings: pdf depends on model, model never on pdf. | io; qpdf 12.4.2 (static, native crypto), libjpeg-turbo 3.2.1 (qpdf needs it), the shared zlib; DirectWrite, Font Subsetting API (fontsub), WIC |
| `services/project` | `XournalProject` | `ProjectTree` (the folder as on disk: Windows-hidden/system items left out, links shown but not entered, File Explorer's order - `CompareStringEx` with `SORT_DIGITSASNUMBERS`, ties ordinal), `ProjectFiles` (name validation, "Name (2)", `IsAtOrBelow`/`MapRenamed`/`SamePath`, rename exactly one item with `MoveFileEx`, new folder, recycle bin with `IFileOperation`), `FolderWatcher` (`ReadDirectoryChangesW` on its own thread, hidden folders such as .git ignored, nothing escapes the thread) | base; shell32, ole32 |
| `services/git` | `XournalGit` | `GitSync` (status with folders: modified > new, renamed shown as modified; sync = commit all, fetch, rebase or fast-forward, push, a conflict aborted and thrown as `SyncConflict`; `Discard` of one file, its committed path found ignoring case but never guessed), `GitCredentials` (Credential Manager: `CredReadW` of `git:https://host`, UTF-8 or UTF-16 secrets) | base; libgit2 1.9.7 (ExternalProject, static, WinHTTP, the shared zlib) |
| `services/tasks` | `XournalTasks` | `TaskFile` (todo.txt line edits, re-read before each change, line endings/encoding/final newline kept, ANSI refused) | io |
| `services/recovery` | `XournalRecovery` | `Recovery` (copy + info JSON with the writing process' id and start time in `<app folder>\Recovery-native` - apart from the C# app's `Recovery` folder, so neither app sees the other's copies; abandoned = that process is gone; recycle bin under a readable name), `Autosave` (ordered write/delete queue on a worker; the app's UI decides when) | io, project; Windows.Data.Json |
| `services/settings` | `XournalSettings` | `Settings` (`settings-native.json` with Windows.Data.Json, unknown keys kept; never the C# app's settings.json): window, last file, project folder, pane, done tasks shown, tool choices (InkPages' serialized string), document positions per file | io; Windows.Data.Json |
| `pdftext` | `XournalPdfText` | PDF text and search (`TextDocument` from the PDF's bytes: `Text(page)` = text + a box per character, `Find`/`FindAll` = hits with one rect per line and a context snippet, case/whole word, cancellable; places in `pdf::PageFrame`'s shown coordinates). PDFium is loaded on first use (`Available()`); a missing or broken `pdfium.dll` is logged and only disables search. Every PDFium call is serialized process-wide. An exe that links it calls `xournal_deploy_pdfium(<target>)` (DLL + `licenses/pdfium` next to it). Measured, 500 pages of text: open 0.4 ms, `Text` 1.3 ms/page, `FindAll` ~0.6 s (1.2 ms/page) - off the UI thread. | pdf; PDFium 158.0.8125.0 (DLL, bblanchon/pdfium-binaries, delay-loaded), delayimp |
| `import` | `XournalImport` | The iPad import (Datei -> PDF mit iPad-Anmerkungen importieren; port of the C# `PdfMarkupImport`, `AppleMarkup`, `PencilKitDrawing`): `Prepare` splits an annotated PDF into the PDF without Apple's markup plus .xopp elements - PencilKit strokes (erased pieces with masks and ranges, transforms, width factors 0.4 pen / 1.2 marker), `/FreeText` as `<text>`, every other shown annotation as a picture; `RenderPictures` renders those with the pdf module's `PdfAreaRenderer` (Windows.Data.Pdf, transparent); `CreateDocument`. Annotations read with qpdf; the NSKeyedArchiver binary plist by `src/BinaryPlist` (no small permissive C++ library: libplist is LGPL, others need Boost). | pdf; protozero 1.8.2 (BSD-2, header-only protobuf decoder), qpdf, crypt32 (base64) |
| `app` | `XournalStudio` (exe) | The window and its feature classes (see "The app" below): menu, InkPages' writing surface and toolbar, side pane, messages, document session, the ink bridge, the page underlay, PDF export, page commands; the services' panes and controllers | all modules above; InkPages (submodule `external/InkPages`, only its library is built here); comctl32 v6, Direct2D/DirectWrite, Direct3D 11, WIC, Windows.Data.Pdf |
| `tests` | `XournalTests` | Unit tests of all modules, ported from `XournalStudio.Tests` with the same sample XML, plus a lossless round trip of `examples\*.xopp`; the pdf tests generate their PDFs and inspect the output with qpdf | io, pdf, GoogleTest; `tests/services` (`XournalServicesTests`): the services, in a single-threaded apartment |

## Rules (from the repository's CLAUDE.md)

- Upstream .xopp format only; never invent attributes or companion files.
- Never delete from files: the XML tree keeps every element, attribute (in order), text, comment and processing
  instruction; saving may add formatting whitespace only.
- Never swallow errors: every Windows/COM call is checked; failures throw `xournal::Error` (HRESULT + text) and are
  logged where they are thrown.
- Small classes with one job; RAII (`ComPtr`, `unique_ptr`); a small public API per module.

## The ink layer (InkPages)

The model does not depend on InkPages. A page's strokes reach the ink layer as plain `StrokeInk` (samples with
pressure, color, size, highlighter) through `PageInk`:

1. `PageInk::ReadStrokes()` (any thread, page without view) → `InkRecord{ink, element}` for every stroke that can be ink.
2. The ink layer creates its strokes and calls `Attach(view, {(id, element)...})`; `view` is an `InkView` the app
   implements over the presenter (stroke ids in drawing order, transform, samples).
3. While attached, `Snapshot()` writes unchanged strokes as their original elements, new ones converted once per
   transform, erased ones left out; previews (`BeginPreview`/`EndPreview`), `RecordTransform`, `RenameStroke` (undo's
   copies), `Adopt` (pasted elements), `ElementOf`.
4. `Detach()` writes the ink into the page's element; the page holds no ink again.

Xournal's width is the width at full pressure; Windows ink's size the width at half pressure (`InkWidth`, factor 1.75).

## The app

`app/` builds `XournalStudio.exe` (WIN32, static CRT; manifest: comctl32 v6, PerMonitorV2, UTF-8). `MainWindow` only wires
the window's parts to feature classes:

- `MainMenu` (Datei/Bearbeiten/Seite/Ansicht with the C# app's shortcuts; features of later phases shown disabled),
  `AppToolbar` (InkPages' toolbar in German with the exe's pen cursors and the app's buttons: Speichern, Als PDF
  exportieren | Seite einfügen, Seite löschen - the page buttons drawn from path data), `SidePane` (Dateien, Aufgaben,
  Seiten), `Toast` (a layered popup at the bottom of the surface; Windows 11 InfoBar colors with the system icons;
  success/info close after 5 s, warnings and errors stay), `Dialogs` (file dialogs, the leave question).
- `DocumentSession`: new, open (.xopp, .moj, or a PDF as a new document; loaded on the thread pool), save, Save As with
  attachments, the leave question (Speichern / Nicht speichern / Abbrechen); saving only by choice.
- `DocumentView`: the shown document on InkPages' `WritingSurface` - page sizes from the model, plain paper in the page's
  color (the underlay draws the real background), the ink through the bridges, the underlay, the file's pictures.
  It must go before the surface: its destructor takes its callbacks back; `load`/`store`/`pagesRemapped` hold the
  shared `PageInkBridges`, never the view.
- `PageInkBridge` (one per shown document, `PageInkBridges` the set): lazy pages between InkPages and `PageInk`. `load`
  hands out a page's strokes with keys (`KeyedStroke`); `store` and, before a snapshot, `StrokesOf` bring them back: a
  keyed stroke is written as its original element (moved or scaled by the key's transform with
  `Strokes::Transform`, widths unchanged), key 0 is new ink (converted). load/store/pagesRemapped run on InkPages' ink
  thread, in order; the bridge keeps the ink side's own page list for them.
- `PageUnderlay` (Direct2D into InkPages' page underlay, upstream's order: background - paper color and
  `BackgroundRuling`, the PDF page, a picture - then each layer's drawn strokes, pictures and texts) with
  `PdfBackground` (the background PDF loaded from memory, pages rendered on `RenderQueue`'s thread by
  `PdfPageRenderer` - `IPdfRendererNative` on the app's own D3D11 device, released while the app runs - at the settled
  zoom, nearest page first, cancellable), `PictureBitmaps` (WIC; `<teximage>` PDFs rendered), `TextDrawing`
  (DirectWrite).
- `FilePictures`: InkPages' `PictureHost` for the file's pictures; moves write the picture's attributes in place, a
  picture moved onto another page goes to its top layer.
- `PdfExportCommand` (Datei -> Als PDF exportieren with the pdf module), `PageCommands` (Seite einfügen: a graph page of
  the size of the page in view after it; Seite löschen: asked, cannot be undone, the last page stays). Page operations
  keep the ink's undo history (InkPages' `InsertPages`/`RemovePage`); the app's per-page data follows the same `PageMap`.
- Logs: `XOURNAL_STUDIO_LOG` (InkPages' lines in `<log>.inkpages.log` beside it), errors always in
  `%LOCALAPPDATA%\XournalStudioForWindows\errors.log`; `XOURNAL_STUDIO_CONFIG_DIR` for tests.

Sandbox tests (`UiTests/`, InkPages' runner and TestKit; `Run-AppTest.ps1` waits for a free sandbox and holds
`.local-build\sandbox.lock`): `InkRoundTripTest` (write, move, undo, erase, save, reload: untouched strokes byte-identical,
moved ones shifted exactly), `DocumentFlowsTest` (PDF as a new document, the leave question, Save As with an attachment,
export), `PagesFlowTest` (a text, the pictures, insert and delete a page with undo on another page, the toolbar's
buttons by `uiaprobe`).
