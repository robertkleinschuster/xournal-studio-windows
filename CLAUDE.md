# CLAUDE.md

Guidance for Claude Code in this repository. Keep it current: add what is learned, remove what turns out wrong.
Platform facts (Windows ink, Windows.Data.Pdf, sandbox, diagnostics) live in the user's global CLAUDE.md; the writing
surface's facts in `external/InkPages/CLAUDE.md`. This file holds what is this app's own.

## What this is

**Xournal Studio for Windows** (short name in the app: **Xournal Studio**; installed as **Xournal Studio (Win32)**) – a
small native C++20/Win32 note app for Windows 11 that reads and writes Xournal++ `.xopp` files. It is a fresh iteration
of the GTK-based "Xournal Studio" (a personal Xournal++ fork, https://github.com/robertkleinschuster/xournalpp), not a
port: features may work differently when a simpler or more Windows-native way exists.

History: GTK fork → C#/WinUI 3 app (2026-11-15 … 11-19; **tag `csharp-final`**, release v0.1.0; it stays installed on
the user's machine in `%LOCALAPPDATA%\Programs\Xournal Studio for Windows` – never touch that folder or its data) →
this C++/Win32 app (`docs/CPP-UMZUG.md`; moved from `native/` to the root on 2026-11-19, first release v0.2.0).

The user is a student on a Lenovo ThinkPad X13 Yoga Gen 4 (convertible, Wacom AES pen, 1920x1200 at 150 %,
right-handed) who annotates lecture PDFs and solves exercises in a git-synced folder. Typical workflow (see
`examples/`): get an exercise PDF → insert graph pages → copy parts of the exercise onto them → write the solution with
the pen; electrical engineering exercises have circuit diagrams (lines, rectangles, circles, arrows), math exercises
long formulas in several colors. For open pen/UI design questions, propose how OneNote does it.

## Constraints (agreed with the user)

- **Direction (user, 2026-11-19): "nativ, schlank, Win32 – aber die moderne Variante davon".** Raw C++20/Win32:
  comctl32 v6 with visual styles (Windows 11 look), PerMonitorV2, DirectComposition/Direct2D/DirectWrite/WIC, Windows
  ink through the Win32 ink presenter (InkPages), DirectManipulation, C++/WinRT for WinRT APIs (Windows.Data.Pdf,
  InkAnalyzer); few dependencies, statically linked; fast start.
- **Doctrine: Windows' APIs do the work, the app only wires them up correctly.** Every feature is built the way
  Microsoft builds it (docs, samples, the controls' own extension points). App code connects input, feedback, undo and
  the .xopp mapping – nothing more. When something seems to need own logic, first look for the API that does it; when
  an API is broken, find the root cause and stay as close to Microsoft's intended route as possible. Rebuilding
  existing code to get closer to this is welcome.
- **No home-made solutions, no hacks.** Use what exists: Windows features/APIs and established libraries. Third-party
  code only where Windows has nothing (zlib, qpdf, libgit2, protozero, PDFium – `THIRD-PARTY.md`), pinned. Do not work
  around platform behavior (toggling input devices, hit-testing tricks, hooks, timers fighting the framework). If the
  native way does not work, stop and report it with the options.
- **Never swallow errors** (app code, handlers, experiments): every Windows/COM call is checked; failures throw
  `xournal::Error` (HRESULT + system text) and are logged where thrown. Before concluding "the platform does not
  raise X", prove the handler ran with a log line at its very start.
- **Upstream .xopp format.** Every feature is expressed with what upstream Xournal++ writes
  (`src/core/control/xojfile` of xournalpp/xournalpp: XmlTags.h, XmlAttrs.h, SaveHandler.cpp). Never invent
  attributes, elements or companion files – the GTK fork did (`portal` attributes, `.portals.pdf`, `.pages.pdf`,
  `fork-export`) and that is exactly what this app must not do. Copied PDF areas are plain `<image>` PNGs. Do not take
  format details from the GTK fork's source; check upstream.
- **Never delete from files.** Everything read is written back, including elements/attributes the app does not
  understand or that other programs added (the XML tree keeps every element, attribute in order, text, comment and
  processing instruction; saving may add formatting whitespace only). Only the content the user changed is rewritten.
- **Native elements – no Frankenstein** (user, 2026-11-19): the controls of Win32 in their default look (comctl32
  menus/tree/toolbar/list, `TaskDialog`, common file dialogs, DirectManipulation for finger pan/zoom, Windows ink via
  the Win32 ink presenter); no UI framework on top, no XAML islands, no rebuilt controls.
- **Light only for now** (user, 2026-11-19): Win32 has no documented dark mode for comctl32 controls (Microsoft Learn
  "Support Dark and Light themes in Win32 apps" documents only detection and the dark title bar); half dark would be
  Frankenstein, the undocumented uxtheme routes a hack. Revisit once Microsoft documents it.
- **Icons and colors** (user, 2026-11-19): commands with Segoe Fluent Icons (undo/redo = the official E7A7/E7A6), own
  path icons in the same style only where no documented glyph fits (glyph list: `~/.claude/docs/segoe-fluent-icons.txt`);
  files and folders in the explorer tree with the shell's system image list; messages with the system's
  info/warning/error icons (`TaskDialog`, `SIID_*`); toasts in Windows 11 InfoBar colors.
- **Pen, finger, mouse** (user, 2026-11-19): only the pen writes. Finger and touchpad pan (with inertia) and zoom the
  sheet, never draw. The mouse never writes: a mouse drag selects (like Auswählen), moves/scales the selection, a click
  beside it clears it, a click on a link follows it; links in a PDF also follow a finger tap. The pen operates every
  control (controls in normal mouse size, not finger size).
- **Pen buttons:** eraser end / eraser button erases with any tool (exact or whole strokes, following the toggle).
  Barrel button (user, 2026-11-15/16): hold barrel = lasso, always, with every tool and without switching the toolbar;
  a barrel drag inside the selection moves it, on a corner handle scales it, a barrel tap clears it. The tip hovering
  over the selection moves/scales it too, elsewhere it inks.
- **Simple, flat and honest – no magic** (user, 2026-11-15). Show things as they are and do exactly what the user asked:
  the explorer shows every file and folder (only what Windows marks hidden/system is left out), nothing is paired,
  filtered, renamed or deleted "along with" something else, opening a PDF does not redirect to a .xopp of the same name
  and does not pick a file name behind the user's back (the first save asks).
- **Saving only by choice, nothing lost** (user, 2026-11-15): whenever a document is left (another opened, window
  closed, file renamed/deleted in the explorer, sync) the same question Speichern / Nicht speichern / Abbrechen; no
  saving on focus loss. Unsaved changes are copied to `%LOCALAPPDATA%\XournalStudioForWindows\Recovery-native` (never
  next to the document: the folder is git-synced; apart from the C# app's `Recovery`) 5 s after a change and on focus
  loss; a copy of a session that ended without a decision is offered at the next start (Wiederherstellen / Verwerfen =
  recycle bin / Später). New documents are named like Xournal++ (`%F-Notiz-%H-%M`) or after their PDF.
- **One selection tool** (user, 2026-11-15): Auswählen = lasso or rectangle for strokes, pictures and texts; selected,
  pasted and duplicated content is handled the same way: move, scale (corner handles only, proportional, stroke width
  unchanged), duplicate, cut/copy, delete; across pages with auto-scroll, one undo step per drag. After paste/duplicate
  the Auswählen tool stays active.
- **Eraser** (user, 2026-11-16/17/18): one eraser tool and the toggle "Ganze Striche radieren" (off by default: the
  eraser erases exactly – what is inside the circle disappears, cut where the ink's edge meets the circle; radius
  1–20 pt, default 4, shown as a circle at the pen). The toggle never changes the active tool; the pen's eraser end and
  button follow it. User: "wichtig ist dass er robust funktioniert".
- **Pages stay** (user, 2026-11-16): pages as the file has them (upstream keeps every element on one page, sizes and
  backgrounds differ); strokes belong to the page of their first point and are split at the page boundary; a selection
  moves across pages. **Pages are moved with buttons, not drag and drop** (user, 2026-11-20: "drag and drop ist schlecht
  zum seiten verschieben"): Nach oben / Nach unten in the Seiten pane's toolbar, Seite → Seite nach oben / unten, the
  pane's context menu; disabled at the first / last page; selection and view follow the page.
- **Little custom code, strictly encapsulated; no monster classes** (user, 2026-11-15): small classes with one job (a
  few hundred lines at most); `MainWindow` only wires the window's parts to feature classes. Logic without UI goes into
  the modules where the unit tests reach it. Module dependencies only in the direction the README shows.
- **Iterative.** Only features in actual use, simplest version first. Ask before large new areas.
- **Windows 11 only**, this machine's version (26200) – no compatibility layers.
- **Right-handed UI:** nothing interactive at the right screen edge; one toolbar row, left-aligned; menu bar visible;
  the left side is for the side pane (Dateien, Aufgaben, Seiten).
- German UI texts; code, comments and commits in English.

## Build, test, install

```powershell
git submodule update --init                 # InkPages (external/InkPages)
.\build.ps1                                 # Visual Studio's CMake: Release build + unit tests (CTest + GoogleTest)
.\install.ps1                               # HEAD in .local-build\install-source: tests, build, install to
                                            # %LOCALAPPDATA%\Programs\Xournal Studio (Win32) + Start menu, start check
UiTests\Run-AppTest.ps1 -Test UiTests\<Test>.ps1 -Extra <uiaprobe.exe>   # sandbox UI test (below)
installer\Build-Msi.ps1 -Version 0.2.0      # the MSI from .local-build\install-stage (install.ps1 -SkipInstall)
```

Workflow: after a change commit atomically, push and install (`install.ps1` refuses while the installed app runs).

- **install.ps1 is for development, the MSI for releases.** Both use the same folder and Start menu entry
  "Xournal Studio (Win32)". `install.ps1` refuses while the MSI is installed (found by its UpgradeCode): replacing the
  files under Windows Installer's feet would let a repair or uninstall remove the development build. Installing the MSI
  over a folder install.ps1 wrote takes the folder over; uninstall then removes only the MSI's files.
- **MSI** (`installer/XournalStudio.wxs`, WiX Toolset 6.0.2 as a pinned .NET tool in `installer/dotnet-tools.json`):
  per-user, no admin rights, no dialogs (progress only), Start menu shortcut with the app icon, Apps & Features entry
  (icon, version, publisher Robert Kleinschuster), fixed UpgradeCode + MajorUpgrade (a newer version replaces the old).
  Uninstall removes exactly what it installed; settings, recovery copies and documents stay. Unsigned (no code-signing
  certificate): SmartScreen warns on first run. ICE38/64/91 are suppressed (roaming-profile rules; it installs into
  the local AppData); the sandbox test `UiTests/MsiTest.ps1` checks install (silent and with UI), start from the
  shortcut, upgrade and a clean uninstall (in the sandbox every install spent ~2 min in Windows Installer's software
restriction check of the unsigned package - "is not digitally signed", then permitted; run it with
`-TimeoutSeconds 1200`). WiX 7 needs an EULA (OSMF) acceptance – a decision for the user.
- **CI** (`.github/workflows/build.yml`, `windows-2025-vs2026`): every push and PR builds and runs the unit tests. A tag
  `v*` runs `install.ps1 -SkipInstall`, builds the MSI and a portable zip of the staged folder (exe, pdfium.dll,
  licenses\, THIRD-PARTY.md, README.txt), and a separate job (the only one with `contents: write`) creates the GitHub
  pre-release "Xournal Studio (Win32) <version>".

## Architecture

The modules, the ink layer contract and the app's classes are described in `README.md` (keep it current with the
code). In short: `base ← model ← io ← pdf, pdftext, search, import, services/* ← app`; `tests/` per module.

- **InkPages** (https://github.com/robertkleinschuster/InkPages, public, submodule `external/InkPages`; also used by the
  MatheTrainer): the writing surface (`WritingSurface`, one ink presenter for the whole document, lazy pages
  `load`/`store`, DirectManipulation), its toolbar and tools (pens, eraser, selection, shapes, ruler, text), undo, the
  sandbox test kit. Library changes are made, committed and pushed in the InkPages repository, then the submodule
  pointer is committed here. Only its library is built here; its demo and tests run in its own repository.
- `PageInkBridge`: lazy pages between InkPages and the model's `PageInk`; a stroke loaded from the file carries a key
  and is written back as its original element (moved/scaled by the key's transform, widths unchanged); key 0 = new ink.
  Xournal's width is the width at full pressure, Windows ink's size the width at half pressure (factor 1.75).

## Facts learned (keep)

- **PDF rendering on the app's own D3D11 device** (`PdfPageRenderer`: `PdfCreateRenderer` +
  `IPdfRendererNative::RenderPageToSurface`): `PdfPage.RenderToStreamAsync` keeps a static D3D11 device released only
  at DLL detach, where the Intel Iris Xe driver hung process exit (C# app, debugged with cdb 2026-11-16).
- **PDFs are loaded from memory**: a `PdfDocument` from a file keeps the file open (deleting the open document's PDF
  failed with 0x80070005).
- **Errors always land in `%LOCALAPPDATA%\XournalStudioForWindows\errors.log`**; the detailed log only with
  `XOURNAL_STUDIO_LOG` (InkPages' lines in `<log>.inkpages.log`). Settings: `settings-native.json` there (never the C#
  app's `settings.json`).
- `RegisterApplicationRestart` is registered at start: Windows restarts the app after a crash (after 60 s of running);
  the start then offers the recovery copy.
- Upstream .xopp PDF reference: `domain="absolute"` + `filename` = path written by `Util::normalizeAssetPath`
  (absolute or relative to the .xopp, by the path storage mode), or `domain="attach"` + `filename="bg.pdf"` =
  `<name>.xopp.bg.pdf` next to the document. Upstream's default storage mode is relative (a loaded rooted path switches
  to absolute). New documents write the PDF relative to the .xopp with '/'; a loaded reference is written back
  verbatim while it still names the PDF from the save location, otherwise recomputed in its mode.
- .xopp: highlighter alpha 0x7f; only the first PDF background carries `domain`/`filename`; `pageno` may look like
  "3ll" in old files; copied PDF areas of other builds are `<teximage>` with a one-page PDF as base64.
- **Save As with attachments – as Xournal++:** attached PDF and picture backgrounds (`domain="attach"`,
  `<doc>.xopp.<filename>`) are copied next to the new file, which keeps `attach` and refers to its own copy.
- **Pages of another PDF or document** (Seite → Seiten aus PDF oder Dokument einfügen): upstream has one background PDF
  per document, so the chosen pages are appended to the **end** of the document's PDF (the user agreed the original PDF
  may change; appending keeps its page numbers, other .xopp using it stay right), annotations drawn in first; a
  document without PDF takes the chosen PDF. Pages of another .xopp are copied with everything on them; their PDF pages
  are appended the same way. Page inserts are not undoable.
- **PDF export** with qpdf (decided 2026-11-19 over "Microsoft Print to PDF", which outlines embedded fonts, turns
  pictures to JPEG and writes the Windows user name): background PDF pages kept exactly, annotations drawn in (ISO
  32000-1 12.5.5), texts as embedded TrueType subsets. Never written over the background PDF.
- **iPad import** (Datei → PDF mit iPad-Anmerkungen importieren; explicit, never automatic): handwriting = a `/Stamp`
  whose `/AAPL:AKExtras` holds a base64 NSKeyedArchiver plist (`AKInkAnnotation2`) with a PencilKit drawing (protobuf:
  `wrdð`+version or older gzip; page coordinates, top left, points). Erased strokes keep their whole path and list
  visible pieces with parametric ranges and mask polygons (= the visible area). Widths: pen ×0.4, marker ≈ ×1.2
  (→ highlighter); no pressure. Pasted screenshots and other shown annotations become pictures rendered by
  Windows.Data.Pdf (transparent); iPadOS' PaperKit leaves an earlier appearance as a `/Stamp` with only
  `/PPKID`/`/PPKHash` at the drawing's rectangle – dropped with the drawing. `/FreeText` → `<text>`, y = baseline −
  0.905 em. Undocumented formats, decoded with libfreeform's `pkdrawing.rs` and notability-goodnotes' `pencilkit.py`.
- **Explorer order:** names with numbers by value, as File Explorer (`CompareStringEx` + `SORT_DIGITSASNUMBERS`,
  ignore case, ties ordinal).
- Git (libgit2): sign-in = the credential Git Credential Manager / wincred stores in the Windows Credential Manager
  (generic `git:https://host`, GCM as UTF-8, wincred as UTF-16); the app never asks for credentials itself. Sync =
  commit all, fetch, rebase or fast-forward, push; a conflict is aborted.
- Tasks: only `<project folder>/todo.txt`; no reminders, no date words in quick add.
- The page in view is outlined in documents with more than one page.
- **XmlLite writes LF as CR LF** in `WriteString`/`WriteWhitespace` (measured 2026-11-19): `XmlText` writes the pieces
  between line feeds through the writer and each LF with `WriteRaw` – texts and formatting keep LF as upstream writes.
- **comctl32: `LVM_INSERTMARKHITTEST` answers -1 for every point of an `LVS_OWNERDATA` list** (measured with a probe
  2026-11-19); the pages pane has one ordinary item per page (text/image by callback). In icon view with
  `LVS_ALIGNTOP` the insert mark is a vertical bar and before/after follow the horizontal position; `LVS_ALIGNLEFT`
  gives a horizontal mark but wraps items into columns.
- **Clipboard** (`SelectionClipboard`): own format "Xournal Studio Clip" = a `<clip>` of upstream elements as XML (app
  internal, never in a file); PNG + CF_DIB for other apps; pictures of other apps paste as `<image>`; otherwise Windows
  ink's clipboard (InkPages).

## Testing

- **Unit tests** (`tests/`, GoogleTest via CTest, run by `build.ps1`): every module, ported from the C# tests with the
  same sample XML, plus a lossless round trip of `examples\*.xopp`; services tests in a single-threaded apartment.
- **Sandbox UI tests run on a desktop of their own** (user, 2026-11-19): `UiTests\Run-AppTest.ps1 -Test
  UiTests\<Test>.ps1` copies the build's exe and `examples\Schaltungen.*` and runs InkPages' runner
  (`external/InkPages/UiTests/Run-SandboxTest.ps1`: `wsb connect` on a separate Win32 desktop – never the user's
  screen; `-Show` puts it on the user's desktop to watch, tell the user first). The sandbox is shared: the runner
  refuses while `wsb list` is not empty or `.local-build\sandbox.lock` exists. Results per test in
  `%TEMP%\xournal-native-sandbox-<Test>\out` (screenshots, transcript, logs). In-sandbox scripts dot-source
  `C:\sbx\TestKit.ps1`; comctl32 controls and menus are found with InkPages' `uiaprobe.exe` (pass it with `-Extra`).
  Tests: `InkRoundTripTest`, `DocumentFlowsTest`, `PagesFlowTest`, `PagesPaneTest`, `TextToolTest`, `ClipboardTest`, `TasksTest`,
  `RecoveryTest`, `ExplorerTest` (+ `New-TestRepository.ps1`), `MsiTest` (the installer). Inside the sandbox:
  PowerShell 5.1 (Add-Type = C# 5), 1514x900 at 144 DPI; synthetic pen input must hover ~100 ms before a press on the
  selection, as a real pen does. Never name a function `Erase` in a test (PowerShell alias); write scripts with the
  Write tool (shell heredocs turned `\t` in paths into tabs).
- **CI runner differences (2026-11-19):** the runner's `%TEMP%` is an 8.3 short path (`C:\Users\RUNNER~1\...`): the
  git tests compared it with the long path libgit2 reports and failed – the workflow points TEMP/TMP at `RUNNER_TEMP`
  (whether the app should compare paths independent of 8.3 names is open). The runner has only the en-US handwriting
  recognizer ("Hallo Welt" → "Mo11@MelR"): `XOURNAL_TEST_EXCLUDE` (a CTest regex `build.ps1` passes to `ctest -E`)
  leaves out `Search.HandwritingIsRecognisedAndCached` there.
- A second `wsb start` right after `wsb stop` fails with CO_E_APPSINGLEUSE – wait for the old sandbox process.
- Test hooks: `XOURNAL_STUDIO_CONFIG_DIR`, `XOURNAL_STUDIO_LOG`.
- `examples/` holds sanitized real documents; never commit the user's own files (university material, personal data) –
  derive examples with `tools/make_examples.py` and check each page visually.

## Open work

- The parity list and the remaining features are in `docs/CPP-UMZUG.md` (file associations, device test by
  the user).
- Questions for the user: an attached copy `<name>.xopp.bg.pdf` instead of appending to the original PDF (offered, not
  chosen); WiX 7 (EULA) vs. staying on WiX 6; a code-signing certificate.
