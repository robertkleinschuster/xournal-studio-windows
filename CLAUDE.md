# CLAUDE.md

Guidance for Claude Code in this repository. Keep it current: add what is learned, remove what turns out wrong.
Where facts live: general Windows facts (how to work on Microsoft tech, Windows.Data.Pdf, Windows Sandbox, synthetic
input, GitHub runners, cdb, Segoe Fluent Icons) in the user's global CLAUDE.md; Windows ink, the Win32 ink presenter,
DirectComposition, DirectManipulation, comctl32 toolbar, RichEdit and UI Automation in `external/InkPages/CLAUDE.md`.
This file holds what is this app's own.

## What this is

**Xournal Studio for Windows** (short name in the app: **Xournal Studio**; installed as **Xournal Studio (Win32)**) – a
small native C++20/Win32 note app for Windows 11 that reads and writes Xournal++ `.xopp` files. It is a fresh iteration
of the GTK-based "Xournal Studio" (a personal Xournal++ fork, https://github.com/robertkleinschuster/xournalpp), not a
port: features may work differently when a simpler or more Windows-native way exists.

History: GTK fork → C#/WinUI 3 app (2026-11-15 … 11-19; **tag `csharp-final`**, release v0.1.0; it stays installed on
the user's machine in `%LOCALAPPDATA%\Programs\Xournal Studio for Windows` – never touch that folder or its data) →
this C++/Win32 app (plan and parity list: `docs/CPP-UMZUG.md`; first release v0.2.0).

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
- **Native elements – no Frankenstein** (user, 2026-11-19): the controls of Win32 in their default look (comctl32
  menus/tree/toolbar/list, `TaskDialog`, common file dialogs, DirectManipulation for finger pan/zoom, Windows ink via
  the Win32 ink presenter); no UI framework on top, no XAML islands, no rebuilt controls.
- **Doctrine: Windows' APIs do the work, the app only wires them up correctly.** Every feature is built the way
  Microsoft builds it (docs, samples, the controls' own extension points). App code connects input, feedback, undo and
  the .xopp mapping – nothing more. When something seems to need own logic, first look for the API that does it; when
  an API is broken, find the root cause and stay as close to Microsoft's intended route as possible. Rebuilding
  existing code to get closer to this is welcome.
- **No home-made solutions, no hacks.** Use what exists: Windows features/APIs and established libraries. Third-party
  code only where Windows has nothing (zlib, libjpeg-turbo, qpdf, libgit2, protozero, PDFium – `THIRD-PARTY.md`),
  pinned. Do not work around platform behavior (toggling input devices, hit-testing tricks, hooks, timers fighting the
  framework). If the native way does not work, stop and report it with the options.
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
- **Light only for now** (user, 2026-11-19): Win32 has no documented dark mode for comctl32 controls (Microsoft Learn
  "Support Dark and Light themes in Win32 apps" documents only detection and the dark title bar); half dark would be
  Frankenstein, the undocumented uxtheme routes a hack. Revisit once Microsoft documents it.
- **Shared UI and toolbar icons** (user, 2026-11-20): develop Xournal Studio and its InkPages submodule together.
  InkPages owns the single toolbar design and icon set. Xournal Studio supplies its commands and uses those icons.
  Use simple, recognisable symbols from InkPages, rendered as bitmaps at the toolbar's DPI.
  In reviews, move duplicated UI behavior into InkPages when both projects can share it. Preserve the app icon.
  Use the shell image list for files and folders, native TaskDialogs for decisions, and the status bar for feedback.
- **Pen, finger, mouse** (user, 2026-11-20): only the pen writes. Finger and touchpad gestures pan (with inertia) and zoom
  the sheet. Touchpad pointer movement, clicks and drags follow the mouse column.

  | Tool | Pen | Mouse and touchpad pointer | Finger |
  | --- | --- | --- | --- |
  | Pen, highlighter, shapes, eraser | Writes, draws or erases | Left drag selects PDF text through ink or pans | Pan and zoom, tap a link, hold on PDF text to select its word and offer Kopieren |
  | Auswählen | Lasso or rectangle | Left drag uses the current selection mode | Tap selects a stroke, picture or text, drag on its frame moves or scales it |
  | Text | Tap places or edits a text | Click places or edits, drag outside the editor follows viewer behavior | Tap places or edits, drag pans |

  Right mouse drag lassos with every tool. Right click opens the native context menu. Pen and mouse can move or
  scale a selection with every tool. A click beside it or Esc clears it. In Auswählen a second finger during a
  selection drag returns both contacts to DirectManipulation. Mouse clicks and finger taps follow PDF links in the
  viewer. The Text tool takes its clicks and taps. System cursors show the mouse's action on the page. The pen
  operates every control in its normal mouse size.
- **Pen buttons:** eraser end / eraser button erases with any tool (exact or whole strokes, following the toggle).
  The barrel button lassos with every tool. Inside a selection, a barrel tap duplicates it and a barrel drag moves it. The tip moves
  the selection, scales it at a corner and rotates it at the rotation handle. The frame also offers delete and
  duplicate. An inside tap keeps the selection; a tap outside clears it.
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
- **Right-handed UI:** nothing interactive at the right screen edge; toolbar left-aligned and wraps when needed; menu bar visible;
  the left side is for the side pane (Dateien, Seiten, Gliederung, Suche, Aufgaben).
- German UI texts; code, comments and commits in English.

## Process (agreed with the user, 2026-11-20)

- **Agents push to main, no PRs.** Commit atomically and only your own paths (`git commit -m ... -- <paths>`, never
  `git add -A`: other agents work in the same tree); rebase right before pushing.
- **CI is the integration build** (`.github/workflows/build.yml`): it builds and runs all unit tests for every push to
  main. **Builds are batched:** agents push in bursts, CI builds only the newest commit (a newer push cancels the
  running build); local builds stay rare.
- **One build at a time** on the machine (CPU, LNK1104 file locks across worktrees): `build.ps1`, `install.ps1` and
  `tools\with-build-lock.ps1 <command>` (for raw `cmake --build`/`ctest`) hold the named mutex `Global\XournalStudioBuild`
  (`tools/BuildLock.ps1`); a second build waits and logs the holder (`%LOCALAPPDATA%\XournalStudioLocks\
  build-lock-holder.txt`); Windows releases it when the script ends in any way. Never build outside the lock.
- **Targeted tests for small changes:** build and test only what changed (`.\build.ps1 -Target XournalTests -Tests
  '^Pdf'`), so the lock is held briefly; leave the full run to CI.
- **UI tests run on GitHub** (`.github/workflows/ui-tests.yml`) after each green Build; the local sandbox is for
  debugging a test and for `InstallerTest`.
- Joint round after a batch: wait for its CI run (`gh run watch`), send errors to their owners, install the CI build
  (`.\install.ps1 -FromCi`), check the UI test summary.

## Build, test, install

```powershell
git submodule update --init                              # InkPages (external/InkPages)
.\build.ps1 -Target XournalTests -Tests '^Xml'           # targeted: cmake --target (several allowed) + ctest -R;
                                                         # -Target without -Tests runs no tests; -NoTests; -Config Debug
.\build.ps1                                              # everything + all unit tests (rarely: CI does this)
.\install.ps1 -FromCi                                    # install the newest green main run's app (-Commit <rev>)
.\install.ps1                                            # build HEAD locally in .local-build\install-source, install
gh workflow run ui-tests.yml -f tests=TasksTest,TextToolTest [--ref <branch>] [-f build_run=<id>]
UiTests\Run-AppTest.ps1 -FromCi -Test UiTests\<Test>.ps1 -Extra <uiaprobe.exe>   # local sandbox, for debugging
installer\Build-Installer.ps1 -Version 0.2.0              # setup from .local-build\install-stage (install.ps1 -SkipInstall)
installer\Build-Installer.ps1                              # local setup numbered as the next release (newest tag + 1 patch)
```

- `install.ps1` installs to `%LOCALAPPDATA%\Programs\Xournal Studio (Win32)` + Start menu entry, then a start check; it
  refuses while the installed app runs and while the legacy MSI (releases up to 0.3.8) is installed. `-FromCi` downloads the artifact `app-<sha>`
  (`tools\CiArtifact.ps1`, gh) – same checks, folder and start check; it logs run and commit.
- **Build system:** `CMakePresets.json` holds the configuration (also what Visual Studio, VS Code and CLion use):
  configure preset `default` = `Ninja Multi-Config`, binary dir `.local-build\cmake` (outputs `app\<Config>`), the vcpkg
  toolchain (`CMAKE_TOOLCHAIN_FILE=$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`, `VCPKG_TARGET_TRIPLET=
  x64-windows-static`) and ccache; build and test presets `release` / `debug` (tests: parallel, output on failure,
  JUnit file `.local-build\ctest-junit.xml`, no tests found = error; `-R`/`-E` on the command line still work).
  `build.ps1` is a thin wrapper: it enters the VS x64 developer environment (`tools/VsDevEnv.ps1`, also reads
  `VCPKG_ROOT` from the user environment because `setx` reaches new processes only; `with-build-lock.ps1` enters it
  too), holds the build lock and calls `cmake --preset default`, `cmake --build --preset`, `ctest --preset`.
- **Third-party libraries: vcpkg in manifest mode** (`vcpkg.json`, `builtin-baseline` = a commit of the vcpkg clone at
  `VCPKG_ROOT`; zlib pinned to 1.3.1 by `overrides`): zlib, libjpeg-turbo, qpdf, libgit2 (default features: WinHTTP,
  no SSH; external PCRE and http-parser ports), protozero (header-only, found with `find_path`). They are built for
  `x64-windows-static` (static libs, static CRT) at configure time into `<build>\vcpkg_installed` and come from vcpkg's
  **binary cache** (default `%LOCALAPPDATA%\vcpkg\archives`, shared by all worktrees; CI: files provider
  `VCPKG_DEFAULT_BINARY_CACHE` + actions/cache). The configure copies each port's `share/<port>/copyright` to
  `<build>\licenses\<port>` (install.ps1 stages them). qpdf's CMake package names `zlib`/`jpeg` without path: a
  `link_directories` on vcpkg's lib folder resolves them. PDFium (prebuilt, no port): `FetchContent` URL + SHA-256 in
  `pdftext/CMakeLists.txt`. GoogleTest: FetchContent per tree. ccache is the installed tool (`find_program`), not a
  library; the launcher settings (`CCACHE_BASEDIR` = source root, `CCACHE_NOHASHDIR`, `compiler_check=content`, the
  /showIncludes prefix of the installed MSVC language – a German MSVC writes "Hinweis: Einlesen der Datei:") let all
  worktrees share `%LOCALAPPDATA%\ccache` / `CCACHE_DIR`. Debug info `/Z7` (ccache cannot cache `/Zi`).
- **New machine:** `winget install Microsoft.VisualStudio.BuildTools --override "--passive --config <repo>\.vsconfig"`
  (or Visual Studio 2026 with C++ and CMake), `winget install Ccache.Ccache --scope user`, `git clone
  https://github.com/microsoft/vcpkg D:\src\vcpkg` + `D:\src\vcpkg\bootstrap-vcpkg.bat -disableMetrics` + `setx
  VCPKG_ROOT D:\src\vcpkg`, then `git submodule update --init` and `.\build.ps1`. The first build of the libraries
  takes minutes (both configurations); later trees restore them from the binary cache in seconds.
- **CI details** (`build.yml`, `windows-2025-vs2026`, actions pinned by SHA): push to main, tags `v*`, by hand;
  docs-only pushes (`**.md`, `docs/**`, `examples/**/*.png`, ...) build nothing; tags are never cancelled. vcpkg
  (the runner's clone, baseline commit fetched) with its binary cache folder, and ccache via actions/cache (saved
  also from cancelled runs). Tests run in parallel; the job summary lists
  compiler errors and failed tests (`gh run view <id> [--log-failed]`). Every main push and tag runs `install.ps1
  -SkipInstall` and uploads `app-<sha>` (exe, pdfium.dll, licenses\; 5 days). A tag also builds the setup and a portable
  zip (+ THIRD-PARTY.md, README.txt); a separate job (the only one with `contents: write`) creates the pre-release
  "Xournal Studio (Win32) <version>". Measured (2026-11-20, 4-core runner, whole job): Ninja + ccache **2.0 min warm**
  (configure 12 s, build 1.5 min), 8.9–15.5 min cold (was ~15–16 min per push). The hosted images alternate between two
  default MSVC toolsets (14.51/14.52), each with its own caches; pinning 14.44 does not help (its cl.exe differs per
  image). Runner quirks: `%TEMP%` is an 8.3 path, so the workflow points TEMP/TMP at `RUNNER_TEMP` (libgit2 reports long
  paths; whether the app should compare paths independent of 8.3 names is open); only the en-US handwriting recognizer,
  so `XOURNAL_TEST_EXCLUDE` (a CTest regex passed to `ctest -E`) leaves out `Search.HandwritingIsRecognisedAndCached`.
- **install.ps1 is for development, the setup for releases.** Both use the same folder and Start menu entry and may
  replace each other (Inno Setup's uninstall removes only the files it installed). Only the legacy MSI of the releases
  up to 0.3.8 is refused (Windows Installer's repair/uninstall would remove the development build): uninstall it once.
- **Setup** (`installer/XournalStudio.iss`, Inno Setup 6.7.3 = ISCC.exe, a native compiler; locally `winget install
  JRSoftware.InnoSetup --scope user`, on the CI `choco install innosetup`; replaced the WiX MSI 2026-11-23, no .NET needed):
  per-user, no admin rights, one page ("Bereit zur Installation" – Inno shows it when no page came before; progress
  after), Start menu shortcut with the app icon, Apps & Features entry (icon, version, publisher Robert Kleinschuster),
  fixed AppId: a setup of the same or a newer version installs over the installed one. Local builds are numbered as the
  next release (newest tag + 1 patch, `-local`). Uninstall removes exactly what it installed; settings, recovery copies
  and documents stay. Unsigned: SmartScreen warns on first run. `InstallerTest` (sandbox only) installs an older
  and the new setup over it, starts the app from the shortcut, uninstalls silently and with the wizard.

## Architecture

The modules, the ink layer contract and the app's classes are described in `README.md` (keep it current with the
code). In short: `base ← model ← io ← pdf, pdftext, search, import, services/* ← app`; `tests/` per module.

- **InkPages** (https://github.com/robertkleinschuster/InkPages, public, submodule `external/InkPages`; MatheTrainer pins
  its WinUI tag `winui-final`): the writing surface, its toolbar and tools, undo, the sandbox test kit. Library changes
  are made, committed and pushed in the InkPages repository, then the submodule pointer is committed here. Only its
  library is built here; its demo and tests run in its own repository.
- `PageInkBridge`: lazy pages between InkPages and the model's `PageInk`; a stroke loaded from the file carries a key
  and is written back as its original element (moved/scaled by the key's transform, widths unchanged); key 0 = new ink.
  Xournal's width is the width at full pressure, Windows ink's size the width at half pressure (factor 1.75).

## Facts learned (keep)

- **PDF backgrounds:** rendered by `PdfPageRenderer` with `IPdfRendererNative` on the app's own D3D11 device and loaded
  from memory (why: global CLAUDE.md, Windows.Data.Pdf – the static-device exit hang and the open file).
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
- **Tabs** (user, 2026-11-27, "wie in einer IDE"): every open document is a tab (`TabStrip` = comctl32 tab control above the
  pages; Strg+W, Strg+Tab, middle click and the tab's menu close/switch). One WritingSurface shows one tab; the others
  are parked in `DocumentSession` (ink brought into the model by `CaptureInk` only when changed, unsaved flag and view
  place kept with the tab) - the surface's undo history starts over when a tab is shown again. New/Open add a tab (a
  file already open is only shown, an untouched new document is replaced); closing asks per tab, the window asks for
  every tab with changes; the tabs of the last session return at the start (`Settings.openFiles` + `lastFile`);
  recovery copies exist per document; file operations in the explorer and a sync's pull close and reopen affected tabs
  at their place (`DropTab` + `Open(file, position)`). While a question is open a document that finishes loading is
  added to the strip without being shown (`QuestionScope`). Reviewed by an Opus agent 2026-11-27 (findings fixed).
- **iPad export** (user, 2026-11-27: the counterpart of the import, "bearbeitbares PDF"; Datei -> Für iPad exportieren):
  the normal PDF export (background pages kept, texts, pictures drawn in) with the opaque round-ended pen strokes taken
  out of the page content and written as one `/Stamp` per page, as iPadOS writes it: `/AAPL:AKExtras` with
  `/AAPL:AKAnnotationV2` (base64 NSKeyedArchiver bplist: `AKInkAnnotation2`, object graph copied from the user's PDFs -
  `AppleMarkup::WriteInk`) and `/AAPL:AKPDFAnnotationDictionary` (copy of the annotation's own keys), `/F 644`, `/T`,
  an `/AP` form with the strokes as vector paths (every viewer shows them; iPadOS draws its own on edit), `Rect` = the
  strokes' box. The drawing is the **older gzip variant** (`PencilKitDrawing::WriteLegacy`): fields surveyed over the
  user's 10 older drawings (drawing: 1=10, two 16-byte ids, {n,0,n}, {0,1,0}, inks #4 {color, name, 3}, strokes #5
  {i,0,i+1}, {0,0,0}, ink index, point count, 24-byte points with force/aspect 1000, bounds, Apple time, two ids; then an
  id and {0,1,0}); the `wrd` variant of newer files and `/PPK` (PaperKit) are not written. Brush size = width / 0.4.
  PencilKit's points are the control points of a smooth curve (uniform cubic B-spline; the user's own points lie
  ~0.8 pt apart, 2.1 at most), so the polyline is written resampled to 0.8 pt (widths interpolated, a dot as a
  sub-point stroke) - found by the Opus review; the importer reads control points as a polyline, so a round trip
  test cannot see it. Stroke bounds, the archive's rectangle, /Rect and /BBox are the same whole numbers (as Apple's).
  Highlighters, dashed, filled and square-ended strokes, strokes below a later picture (an annotation lies over the
  page) and strokes off the page stay drawn in the page (the marker ink's layout is not known).
  **Not verified on an iPad** (none here): checked by reading it back with the importer and by rendering it with
  Windows.Data.Pdf.
- **Explorer order:** names with numbers by value, as File Explorer (`CompareStringEx` + `SORT_DIGITSASNUMBERS`,
  ignore case, ties ordinal).
- Git (libgit2): sign-in = the credential Git Credential Manager / wincred stores in the Windows Credential Manager
  (generic `git:https://host`, GCM as UTF-8, wincred as UTF-16); the app never asks for credentials itself. Sync =
  commit all, fetch, rebase or fast-forward, push; a conflict is aborted.
- **Git status in the status bar** (user, 2026-11-23; first as the sync button's text like VS Code's sync item, 2026-11-20): the
  window's status bar has four parts, left to right: git ("Git: 3  ↑1  ↓2" = changed files a sync commits, commits to
  push, commits to pull as of the last fetch; "Git: aktuell"; tooltip in words; nothing without a repository), the latest
  message, "Seite n von m" and the zoom (right-aligned). The sync button is an icon of fixed size (its text made the
  toolbar jump). Counted with every explorer refresh; a background fetch (remote-tracking refs only) at start, on folder
  change and on window activation at most every 5 minutes (time check, no timer); a sync counts as a fetch; a failed fetch
  is logged and shows "↓?".
- **Vom Server holen** (user, 2026-11-23): a pull-only button next to Synchronisieren (Datei → Vom Server holen too):
  fetch, then fast-forward or rebase own commits; no commit, no upload, no question about saving first. Uncommitted
  changes stay (a fast-forward over a file they touch throws with libgit2's message); a rebase needs a clean tree of
  tracked files and says so. A pull that changes the open document is handled as after a sync (reload / keep mine / copy).
- **Sync with other clients pushing:** fetch, rebase (or fast-forward) and push run in a loop: a push the server refuses
  because it moved since the fetch starts over (up to 4 times); a refusal with nothing new on the server is thrown. A
  rebase conflict is aborted (the local commits stay, the repository is never mid-rebase) and reported as a conflict.
- **Explorer git** (user, 2026-11-20, like IDEs): deleted files of the last commit stay in the tree, red and struck
  through; Änderungen verwerfen on a file or folder: tracked files back from HEAD (deleted ones restored), new files to
  the recycle bin, after a question naming the files; a staged rename = old path restored, new path to the bin.
  Nothing lost (coordinator's decision, 2026-11-20): the current versions of overwritten files go to the bin as copies
  first (`ProjectFiles::RecycleCopies`), and every step that can fail runs before the open document is reloaded.
  Colors only, no status letters (a TreeView shows none without drawing the items itself). Path containment everywhere
  through `Paths::IsAtOrBelow`/`WithoutTrailingSeparator` (base) – one rule.
- **Cut/copy/paste of files** in the tree (user, 2026-11-20; no drag & drop): CF_HDROP + "Preferred DropEffect" on the
  real clipboard (interoperates with File Explorer), copy/move with `IFileOperation` (the shell's conflict UI, undo).
  Moving the open document keeps it open: `MoveFiles` points it (and its PDF/pictures if moved) to the new place and
  marks it changed, so saving writes the PDF reference from there. Closed .xopp files moved away from a PDF they refer
  to relatively keep the old reference (as with File Explorer; the loader then looks next to the .xopp).
- Tasks: only `<project folder>/todo.txt`; no reminders, no date words in quick add. Shown with comctl32 in its
  standard look (user, 2026-11-20: "nutze da die win32 komponenten passender"): a report-view ListView with column
  headers, check boxes, groups Überfällig / Offen / Erledigt, info tips and in-place editing (F2); the "Fällig am:"
  date picker includes its own check box (`DTS_SHOWNONE`). A double-click opens the edit dialog; the form above it in Windows' control
  sizes.
- The page in view is outlined in documents with more than one page.
- **PDF text with the mouse** (user, 2026-11-20: "dass die maus pdf text auswählen kann ... in die zwischenablage"):
  `PdfTextSelection` on InkPages' `mouseClaims`/`mouseGesture` (a drag or double/triple click InkPages hands over where
  no ink, picture, text or selection frame lies; a single click stays InkPages' - links, Text tool). PDFium does the
  geometry (`pdftext::TextPage`: `FPDFText_GetCharIndexAtPos`, `CountRects`/`GetRect`, `GetText`); word = up to spaces
  and line breaks, line = up to PDFium's line breaks. Read-only (Ausschneiden/Löschen ignore it), one page. Escape =
  Bearbeiten -> Auswahl aufheben (clears an ink selection too). The I-beam over text belongs to the hover-cursor hook,
  not to `mouseClaims`.
- The side pane is never narrower than its tabs need (280 DIP or the tab strip's measured width): comctl32's tab
  padding does not grow with the DPI, the text does (at 96 DPI 280 px was too narrow).
- **XmlLite writes LF as CR LF** in `WriteString`/`WriteWhitespace` (measured 2026-11-19): `XmlText` writes the pieces
  between line feeds through the writer and each LF with `WriteRaw` – texts and formatting keep LF as upstream writes.
- **comctl32: `LVM_INSERTMARKHITTEST` answers -1 for every point of an `LVS_OWNERDATA` list** (measured with a probe
  2026-11-19). The pages pane now uses report view and `ListView_GetTopIndex`. In icon view with
  `LVS_ALIGNTOP` the insert mark is a vertical bar and before/after follow the horizontal position; `LVS_ALIGNLEFT`
  gives a horizontal mark but wraps items into columns.
- **Clipboard** (`SelectionClipboard`): own format "Xournal Studio Clip" = a `<clip>` of upstream elements as XML (app
  internal, never in a file); PNG + CF_DIB for other apps; pictures of other apps paste as `<image>`; otherwise Windows
  ink's clipboard (InkPages).
- **Copy a PDF area:** Auswählen → Rechteck. If the rectangle selects no objects, its portion on the PDF page where
  the drag began offers the native menu command "PDF-Bereich als Bild kopieren". Windows.Data.Pdf renders that portion
  to PNG, and pasting writes an upstream `<image>` at the selected size.

## Testing

- **Unit tests** (`tests/`, GoogleTest via CTest): every module, ported from the C# tests with the same sample XML, plus
  a lossless round trip of `examples\*.xopp`; services tests in a single-threaded apartment.
- **UI tests on GitHub (standard since 2026-11-20, user's decision):** `ui-tests.yml` runs every `UiTests\*Test.ps1`
  except `InstallerTest` after each green Build of a push to main (`workflow_run`; no Build for docs-only pushes, so no UI
  tests; a newer Build cancels running UI tests), one `windows-2025` job per test in parallel, with the Build's artifact
  `app-<sha>` (no build) and `uiaprobe.exe` compiled only when its source changes (Actions cache). The summary job lists
  PASS/FAIL per test with links to the artifact `ui-<Test>` (screenshots, transcript, app logs). A job takes ~1–2 min.
  `UiTests\Run-CiTest.ps1` lays out `C:\sbx` as the sandbox has it and runs the script in the runner's interactive
  session – never run it on a developer machine. The runner's screen is 1024x768 at 96 DPI (the sandbox: 1514x900 at
  144 DPI), so layout that only fits at 150 % shows up there (what else the runner offers: global CLAUDE.md).
- Tests: `InkRoundTripTest`, `DocumentFlowsTest`, `PagesFlowTest`, `PagesPaneTest`, `PdfNavigationTest`,
  `PdfTextSelectTest` (a generated PDF: double click = word on the clipboard, a drag = a piece, a drag on ink = lasso),
  `TextToolTest`, `ClipboardTest`, `TasksTest`, `RecoveryTest`, `ExplorerTest` (+ `New-TestRepository.ps1`: a bare
  upstream `remote.git` two commits ahead and one local commit – the sync button's counts; restore, cut/copy/paste),
  `InstallerTest` (the installer, sandbox only). A test declares extra files in a `# Data: examples\X.pdf` line
  (`UiTests\TestData.ps1`, used by both runners); `examples\Schaltungen.*` always comes along. Scripts dot-source
  `C:\sbx\TestKit.ps1` (InkPages); comctl32 controls and menus are found with InkPages' `uiaprobe.exe`.
- **Local sandbox** (debugging, `InstallerTest`): `UiTests\Run-AppTest.ps1 -Test UiTests\<Test>.ps1` (`-FromCi` = the CI
  exe, `-AppDir <folder>`, else the local build) runs InkPages' runner on a desktop of its own (`-Show`: on the user's
  desktop – tell the user first). One test at a time across all worktrees: it waits for the mutex
  `Global\XournalStudioSandbox` (holder logged) and refuses while `wsb list` is not empty. Results in
  `%TEMP%\xournal-native-sandbox-<Test>\out`.
- Test hooks: `XOURNAL_STUDIO_CONFIG_DIR`, `XOURNAL_STUDIO_LOG`.
- `examples/` holds sanitized real documents; never commit the user's own files (university material, personal data) –
  derive examples with `tools/make_examples.py` and check each page visually.

## Open work

- The parity list in `docs/CPP-UMZUG.md` (to be ticked off with commits), file associations (not built yet), the device
  test by the user.
- Questions for the user: an attached copy `<name>.xopp.bg.pdf` instead of appending to the original PDF (offered, not
  chosen); a code-signing certificate.
