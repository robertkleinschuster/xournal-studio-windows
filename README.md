# Xournal Studio for Windows

A small pen note app for Windows 11 convertibles that reads and writes [Xournal++](https://xournalpp.github.io/)
`.xopp` files in the upstream format: nothing Xournal++ does not know is added, nothing the file contains is lost. It
covers one workflow well: annotating lecture PDFs and solving exercises on graph paper with the pen, in a git-synced
project folder.

Native C++20 on Win32 (comctl32 v6, PerMonitorV2, Direct2D/DirectWrite, DirectComposition, DirectManipulation) with
Windows ink through [InkPages](https://github.com/robertkleinschuster/InkPages) (git submodule `external/InkPages`).
Windows APIs first; third-party code only where Windows has nothing (zlib for gzip, qpdf for writing PDFs, libgit2 for
git, protozero for PencilKit, PDFium for PDF text), fetched at pinned releases and linked statically - except PDFium,
a delay-loaded DLL. Licenses: [THIRD-PARTY.md](THIRD-PARTY.md). The earlier C#/WinUI app is at the tag `csharp-final`.

- Pen: Windows ink (pressure, eraser end, barrel button = lasso), pen and highlighter presets, exact or whole-stroke
  eraser, selection (lasso/rectangle: move, scale, copy, delete, across pages), shapes and shape recognition, the
  Windows ruler, a text tool, undo/redo. Finger and touchpad pan and zoom; the mouse selects - only the pen writes.
- Documents: PDF backgrounds (PDF opened as a new document), all Xournal++ backgrounds, pictures, texts, PDF export,
  pages inserted (blank, from a PDF or another document), moved and deleted, iPad (PencilKit) markup import.
- Side pane: the project folder (git colors, sync), todo.txt tasks, page thumbnails, the PDF's outline, search
  (Strg+F: PDF text, texts and handwriting); PDF links follow a mouse click or finger tap.
- Saving only by choice (the leave question), recovery copies of unsaved changes.

## Build, test, install

Requires Windows 11 and Visual Studio 2026 with C++ (its CMake is used). Clone with `--recurse-submodules` (or run
`git submodule update --init`).

```powershell
.\build.ps1            # configure (Visual Studio's CMake, Ninja, ccache), build Release, run the unit tests
.\build.ps1 -NoTests
.\build.ps1 -Target XournalTests -Tests '^Xml'   # one target and the tests matching a CTest regex
.\install.ps1          # the committed state (HEAD, built in a clean worktree): tests, Release build, install to
                       # %LOCALAPPDATA%\Programs\Xournal Studio (Win32) + Start menu entry "Xournal Studio (Win32)";
                       # then a start check. Refused while the installed app runs.
.\install.ps1 -SkipInstall   # build and stage only (.local-build\install-stage)
.\install.ps1 -FromCi       # install the app CI built for the newest green main run instead of building
UiTests\Run-AppTest.ps1 -Test UiTests\TasksTest.ps1 -Extra <InkPages' uiaprobe.exe>   # a UI test in Windows Sandbox
```

CI (`.github/workflows/build.yml`) builds and runs the unit tests on every push to main (docs-only pushes excepted)
and uploads the staged app; `.github/workflows/ui-tests.yml` then runs the UI tests on GitHub-hosted runners. A tag
`v*` publishes the app as an MSI (see Installer) and a portable zip in a GitHub pre-release.

## Layout

```
base/ model/ io/ pdf/ pdftext/ search/ import/ services/   the modules (static libraries, below)
app/                the exe: window, menu, toolbar, side pane, feature classes
tests/              unit tests (GoogleTest, CTest)
UiTests/            UI tests (scripts; Run-CiTest.ps1 on GitHub runners, Run-AppTest.ps1 in Windows Sandbox)
external/InkPages/  the writing surface and its tools (submodule; only its library is built here)
examples/           sanitized example documents (unit tests round-trip them; UI tests use Schaltungen)
docs/               the C++ migration plan (CPP-UMZUG.md)
tools/              make_examples.py (derives examples/), build lock, VS environment, CI artifact download
installer/          the MSI (WiX source, Build-Msi.ps1, the pinned wix tool)
.github/workflows/  CI: build + unit tests, UI tests; releases on tags v*
build.ps1 install.ps1 CMakeLists.txt
```

## Modules

Each module is a static library with its own folder, public headers in `<module>/include/xournal/<module>/` and
internals in `<module>/src/`. The CMake link dependencies are the only allowed direction:

```
base  <-  model  <-  io  <-  pdf, pdftext, search, import, services/ (project, git, tasks, recovery, settings)  <-  app (UI)
```

| Module | Target | Job | Uses |
|---|---|---|---|
| `base` | `XournalBase` | `Error` (HRESULT + system text, logged when thrown), `Log`, UTF-8/UTF-16 and case-insensitive text, `Paths` (full/relative paths as Windows treats them) | Windows |
| `model` | `XournalModel` | The document in memory: XML tree (`Xml`), `XoppDocument`, `XoppPage` with `PageInk` (stroke elements bound to the ink layer) and `PageImages`, `Strokes` (element <-> ink, `StrokeShape`), `XoppImage`, `XoppText`, `XoppClip`, `Background`, `BackgroundRuling`, references (`src/References.h`), `Snapshot`, `Attachments`, `CopyPages`, `DocumentNames`, `TodoTxt`. No file contents read or written, no UI. | base; crypt32 (base64), Windows' ICU (characters) |
| `io` | `XournalIo` | `XmlText` (XmlLite), `Gzip` (zlib), `Files` (whole-file reads, `<file>~` then replace), `DocumentFile` (load/save/Save As with attachments) | model; xmllite, shlwapi, zlib |
| `pdf` | `XournalPdf` | PDF export (`PdfExport`: export pages in the model's terms - `StrokeShape` parts, `BackgroundRuling`, `XoppText`, picture placement; `DocumentExport`: the app's one call, `PrepareExport` on the ink thread + `ExportDocument` on any thread, never over the background PDF) and `PdfPageAppender` (pages of another PDF appended to the document's PDF, annotations flattened; links to the outside - URI actions - stay, internal GoTo links are dropped as they would point into the source); `TextBoxLayout` (the DirectWrite layout of a text box, shared by the export and the search); `PdfAreaRenderer` (areas of pages as PNG with Windows.Data.Pdf on its own Direct3D device, transparent; from a file or from memory); `ClipPicture` (a copied selection as a transparent PNG: exported onto one page in memory, rendered); `PdfNavigation` (from the PDF's bytes: the outline as a tree with named destinations resolved, and a page's links - GoTo or URI - with their areas; places in `PageFrame`'s shown coordinates: crop box turned by /Rotate, as Windows.Data.Pdf shows the page). Background PDF pages are kept exactly (form XObjects; text stays text), texts are embedded TrueType subsets. Draws the model's shapes and rulings: pdf depends on model, model never on pdf. | io; qpdf 12.4.2 (static, native crypto), libjpeg-turbo 3.2.1 (qpdf needs it), the shared zlib; DirectWrite, Font Subsetting API (fontsub), WIC |
| `services/project` | `XournalProject` | `ProjectTree` (the folder as on disk: Windows-hidden/system items left out, links shown but not entered, File Explorer's order - `CompareStringEx` with `SORT_DIGITSASNUMBERS`, ties ordinal), `ProjectFiles` (name validation, "Name (2)", `IsAtOrBelow`/`MapRenamed`/`SamePath`, rename exactly one item with `MoveFileEx`, new folder, recycle bin with `IFileOperation`, `Transfer` = copy/move with `IFileOperation` as File Explorer's paste: the shell's conflict questions, undo record, "Name - Kopie" beside the original, where each item went from `IFileOperationProgressSink`), `ProjectTree::AddMissing` (deleted files of the last commit at their place, marked missing), `FileClipboard` (files on the clipboard as File Explorer has them: CF_HDROP + "Preferred DropEffect"), `FolderWatcher` (`ReadDirectoryChangesW` on its own thread, hidden folders such as .git ignored, nothing escapes the thread) | base; shell32, ole32 |
| `services/git` | `XournalGit` | `GitSync` (status with folders: modified > new, renamed and deleted shown as modified; sync = commit all, fetch, rebase or fast-forward, push, a conflict aborted and thrown as `SyncConflict`; `GetDivergence` = commits ahead/behind the upstream branch from the local references, `Fetch` = the upstream's remote, remote-tracking references only - a fetch and a sync never run at once; `PlanDiscard`/`Discard` of a file or folder: tracked files checked out from HEAD - deleted ones come back -, new files listed for the recycle bin and taken out of the index), `SyncStatus` (the sync button's text "Synchronisieren   3  ↑1  ↓2" and its tooltip lines), `GitCredentials` (Credential Manager: `CredReadW` of `git:https://host`, UTF-8 or UTF-16 secrets) | base; libgit2 1.9.7 (dependency cache, static, WinHTTP, the shared zlib) |
| `services/tasks` | `XournalTasks` | `TaskFile` (todo.txt line edits, re-read before each change, line endings/encoding/final newline kept, ANSI refused) | io |
| `services/recovery` | `XournalRecovery` | `Recovery` (copy + info JSON with the writing process' id and start time in `<app folder>\Recovery-native` - apart from the C# app's `Recovery` folder, so neither app sees the other's copies; abandoned = that process is gone; recycle bin under a readable name), `Autosave` (ordered write/delete queue on a worker; the app's UI decides when) | io, project; Windows.Data.Json |
| `services/settings` | `XournalSettings` | `Settings` (`settings-native.json` with Windows.Data.Json, unknown keys kept; never the C# app's settings.json): window, last file, project folder, pane, done tasks shown, tool choices (InkPages' serialized string), document positions per file | io; Windows.Data.Json |
| `pdftext` | `XournalPdfText` | PDF text and search (`TextDocument` from the PDF's bytes: `Text(page)` = text + a box per character, `Find`/`FindAll` = hits with one rect per line and a context snippet, case/whole word, cancellable; places in `pdf::PageFrame`'s shown coordinates). PDFium is loaded on first use (`Available()`); a missing or broken `pdfium.dll` is logged and only disables search. Every PDFium call is serialized process-wide. An exe that links it calls `xournal_deploy_pdfium(<target>)` (DLL + `licenses/pdfium` next to it). Measured, 500 pages of text: open 0.4 ms, `Text` 1.3 ms/page, `FindAll` ~0.6 s (1.2 ms/page) - off the UI thread. | pdf; PDFium 158.0.8125.0 (DLL, bblanchon/pdfium-binaries, delay-loaded), delayimp |
| `search` | `XournalSearch` | Strg+F over the whole document, no UI: `PrepareSearch` (snapshot, on the ink thread) + `Search` (any thread but an STA; hits page by page, cancellable) over the PDF's text (pdftext), the file's texts (measured on `pdf::TextBoxLayout`, the export's layout) and handwriting recognised by Windows (InkAnalyzer; strokes built with InkStrokeBuilder; `HandwritingCache` keyed by the page's strokes, memory only; `RecognizeAll` warms it in the background). Hits: kind, page, rects in page units (PDF shown coordinates scaled to the page), context. Matching German, the Windows way (`FindNLSStringEx` de-DE: ä = Ä, ß = SS ignoring case; whitespace and line breaks match one space). Measured: recognition ~1-2 s per page of synthetic handwriting (glyph outlines; 234 strokes). | pdftext; DirectWrite, Windows.UI.Input.Inking(.Analysis), NLS |
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

- `MainMenu` (Datei/Bearbeiten/Seite/Ansicht with the C# app's shortcuts (tag `csharp-final`); features of later phases shown disabled),
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
- `SelectionClipboard` (InkPages' `copyToClipboard`/`pasteFromClipboard`): the selection as the app's own format
  "Xournal Studio Clip" (an `XoppClip` of upstream elements as XML: strokes, pictures, texts) between documents and
  windows, plus a picture for other apps (PNG and CF_DIB on white; `pdf::RenderClip` in the background, added while the
  clipboard still holds the copy). Paste: the own format, else another app's picture (PNG, else a DIB) as a new
  `<image>`, else InkPages pastes Windows ink.
- The project folder: `ProjectExplorer` reads it (`ProjectSnapshot`: files, git status, the sync button's counts) on the
  thread pool - on start, after a save or sync, 400 ms after changes on disk (`FolderWatcher`), on activation;
  `ExplorerTree` shows it (git colors; deleted files red and struck through, custom draw's font change) and asks;
  `ExplorerCommands` carries it out (open, new, rename, delete, Änderungen verwerfen of a file or folder, Ausschneiden/
  Kopieren/Einfügen with Strg+X/C/V in the tree - moved files the open document uses are followed by
  `DocumentSession::MoveFiles`, also its own file, so the next save writes the references from the new place).
  `SyncController` syncs; `RemoteFetch` fetches in the background (start, folder change, window activated at most every
  5 minutes; a failure only logged, the button shows "↓?"; a starting sync cancels it through libgit2's progress
  callbacks), so the button can show the commits to pull.
- `PdfExportCommand` (Datei -> Als PDF exportieren with the pdf module), `PageCommands` (Seite einfügen: a graph page of
  the size of the page in view after it; Seite löschen: asked, cannot be undone, the last page stays; Seite nach oben /
  nach unten: one place, the view follows). Page operations keep the ink's undo history (InkPages'
  `InsertPages`/`RemovePage`/`MovePage`); the app's per-page data follows the same `PageMap`.
- `PagesPane` (the Seiten tab): the pages as thumbnails in a comctl32 ListView (icon view, custom draw from
  `PageThumbnails`); a small comctl32 toolbar above it, "Nach oben" / "Nach unten" with Segoe Fluent Icons glyphs
  (`GlyphImages`), moves the selected page (no drag and drop: the user found it bad for moving pages); its context menu
  inserts, moves and deletes pages.
- `TasksPane` (the Aufgaben tab, `<project>/todo.txt`): `NewTaskForm` (text box with cue banner, a "Fällig am:"
  check box with the date picker beside it - enabled while checked, as in Windows' dialogs, in the edit dialog too -,
  Hinzufügen, link to the page in view) above a `TaskListView` - a comctl32 ListView in report view with the
  Explorer theme, column headers Aufgabe / Fällig, check boxes (done), groups Überfällig / Offen / Erledigt with counts,
  info tips, in-place editing (F2; the priority read back as todo.txt writes it, due: and doc: kept - `TodoTxt::EditText`/`WithEditText`) - and "Erledigte anzeigen" below; `TaskEditDialog` (a double-click on a
  row or Bearbeiten in its context menu) edits all of a task. Only the changed line is rewritten (`TaskFile`).
- Logs: `XOURNAL_STUDIO_LOG` (InkPages' lines in `<log>.inkpages.log` beside it), errors always in
  `%LOCALAPPDATA%\XournalStudioForWindows\errors.log`; `XOURNAL_STUDIO_CONFIG_DIR` for tests.

UI tests (`UiTests/`, InkPages' TestKit; on GitHub runners by `ui-tests.yml` after each green build, all but `MsiTest`;
locally in Windows Sandbox with `Run-AppTest.ps1`, which refuses while a sandbox runs and holds the machine-wide mutex
`Global\XournalStudioSandbox`): `InkRoundTripTest` (write, move, undo, erase, save, reload: untouched strokes byte-identical,
moved ones shifted exactly), `DocumentFlowsTest` (PDF as a new document, the leave question, Save As with an attachment,
export), `PagesFlowTest` (a text, the pictures, insert and delete a page with undo on another page, the toolbar's
buttons by `uiaprobe`), `PagesPaneTest`, `PdfNavigationTest`, `TextToolTest`, `ClipboardTest`, `TasksTest`, `RecoveryTest`, `ExplorerTest`, `MsiTest` (the
installer: silent and interactive install, start from the shortcut, upgrade, clean uninstall).

## Installer

`installer/` builds a per-user MSI with the WiX Toolset (pinned .NET tool): `installer\Build-Msi.ps1 -Version <x.y.z>`
from the staged app (`install.ps1 -SkipInstall`). It installs to the same folder as `install.ps1` with the same Start
menu entry, without admin rights, and registers in Apps & Features; a newer version replaces the old one. The MSI is
for releases, `install.ps1` for development: `install.ps1` refuses while the MSI is installed. Unsigned: SmartScreen
warns on the first start ("Weitere Informationen" -> "Trotzdem ausführen").
