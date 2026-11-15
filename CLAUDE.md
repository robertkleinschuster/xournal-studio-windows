# CLAUDE.md

Guidance for Claude Code in this repository. Keep it current: add what is learned, remove what turns out wrong.

## What this is

**Xournal Studio for Windows** (short name in the app: **Xournal Studio**) – a small WinUI 3 / C# note app for
Windows 11 that reads and writes Xournal++ `.xopp` files. It is a fresh iteration of the GTK-based
"Xournal Studio" (a personal Xournal++ fork, https://github.com/robertkleinschuster/xournalpp), not a port:
features may work differently when a simpler or more Windows-native way exists.

The user is a student on a Lenovo ThinkPad X13 Yoga Gen 4 (convertible, Wacom AES pen, 1920x1200 at 150 %,
right-handed) who annotates lecture PDFs and solves exercises in a git-synced folder. Typical workflow (see
`examples/`): get an exercise PDF → insert graph pages → copy parts of the exercise onto them → write the
solution with the pen; electrical engineering exercises have circuit diagrams (lines, rectangles, circles,
arrows), math exercises long formulas in several colors.

## Constraints (agreed with the user)

- **No home-made solutions, no hacks.** Use what exists: WinUI 3 controls, Windows features/APIs (Windows Ink,
  ink analysis, Windows.Data.Pdf, clipboard, pickers, ...) and established libraries (NuGet). Do not re-implement
  something a platform feature or library already does, and do not work around platform behavior (toggling input
  devices, hit-testing tricks, hooks, timers to fight the framework, ...). If the native way does not work, stop
  and report it to the user with the options instead of building a workaround.

- **Upstream .xopp format.** Every feature is expressed with what upstream Xournal++ writes
  (`src/core/control/xojfile` of xournalpp/xournalpp: XmlTags.h, XmlAttrs.h, SaveHandler.cpp). Never invent
  attributes, elements or companion files for our own purposes – the GTK fork did (`portal` attributes,
  `.portals.pdf`, `.pages.pdf`, `fork-export`) and that is exactly what this app must not do. Copied PDF areas
  are plain `<image>` PNGs. Do not take format details from the GTK fork's source; check upstream.
- **Never delete from files.** Everything read is written back, including elements/attributes the app does not
  understand or that other programs (the GTK fork) added. Only the content the user changed is rewritten.
- **Native WinUI 3 controls, default theme.** InkCanvas/InkPresenter (Windows ink: low latency, pressure, pen
  eraser), InkToolbar (incl. custom tool/toggle buttons), ScrollView (touch pan, pinch zoom, touchpad), MenuBar,
  InfoBar, ContentDialog, TreeView, Windows App SDK pickers, Windows.Data.Pdf. Mica backdrop,
  no custom theme or styling.
- **Little custom code, strictly encapsulated.** What is left (the .xopp mapping, glue between controls) lives in
  small classes with one job: `Model/` (file format, unit-tested), `Views/`, `Services/`.
- **Iterative.** Only features in actual use, simplest version first. Ask before large new areas.
- **Windows 11 only**, this machine's version (26200) – no compatibility layers.
- **Right-handed UI:** nothing interactive at the right screen edge; one toolbar row, left-aligned; menu bar
  visible; scrollbars hidden; the left side is for the explorer. Touch never draws (it scrolls/zooms).
- **Pen buttons:** eraser end / eraser button erases (native), barrel button should lasso once the lasso exists.
- **Decisions (2026-11-15):** eraser = native Windows ink eraser (whole strokes; no pixel eraser, Windows has
  none); shape recognition = Windows `InkAnalyzer`; lasso, shape tools and copying PDF areas wait until the
  Windows App SDK delivers `InkPresenter.UnprocessedInput` (no workaround). Parts of an exercise come in via the
  Snipping Tool + paste.
- German UI texts; code, comments and commits in English.

## Build, test, install

```powershell
dotnet build XournalStudio                 # debug build (no Visual Studio needed, .NET 10 SDK)
dotnet test XournalStudio.Tests            # unit tests (file format)
.\install.ps1                              # tests + publish + install to %LOCALAPPDATA%\Programs\Xournal Studio for Windows
```

Workflow: after a change commit atomically, push and install (`install.ps1` refuses while the installed app runs).

## Architecture

- `Model/` – `XoppDocument` (gzip XML; the XML tree is kept as loaded, pages are taken out and put back on save;
  save writes `<file>~` then replaces), `XoppPage` (page element; strokes live in the page's InkStrokeContainer,
  unchanged strokes are written back as their original elements, mapped by ink stroke id; pictures stay XML),
  `StrokeConverter` (Xournal per-segment widths ↔ ink pressure), `XoppImage` (`<image>`/`<teximage>`),
  `XoppFormat` (creator, file version).
- `Views/` – `PageView` (background: color + ruling or PDF page rendered at the current zoom near the viewport;
  pictures layer; InkCanvas), `ToolController` (copies the InkToolbar's tool to all pages' presenters),
  `ExplorerView` + `ExplorerItem` (TreeView of the project folder in the SplitView pane, git colors, sync button).
- `Services/` – `UndoStack` + `PageEdit` (strokes/pictures removed/added; the InkCanvas has no undo),
  `ShapeRecognition` (InkAnalyzer result → clean stroke), `PdfBackground`, `PictureRenderer`,
  `ClipboardService` (bitmap → `<image>`), `AppSettings`, `DebugLog`, `ProjectTree` (which files the explorer
  shows), `GitSync` (LibGit2Sharp: status; sync = commit all, fetch, rebase/fast-forward, push; conflicts are
  aborted), `GitCredentials` (reads Git for Windows' stored credential, see Facts learned).

## Facts learned (keep)

- **The experimental InkCanvas renders and takes mouse/pen input above XAML popups** (ink over ContentDialogs, clicks on
  dialog buttons drew instead; keyboard worked; no child HWND involved). Agreed with the user: `ShowDialogAsync`
  hides the pages' InkCanvas while a dialog is open – use it for every dialog. Menus/flyouts over a page may be
  affected too (not checked). Re-test and remove with each new Windows App SDK release.
- Strokes are polylines (`FitToCurve = false`, loaded and drawn): with Bezier fitting the few-point rectangles of
  the user's circuit diagrams (resistors) came out round.
- Git: LibGit2Sharp (user's choice over the git CLI). Sign-in = the credential the Git Credential Manager / wincred
  helper stores in the Windows Credential Manager (generic `git:https://host`, GCM as UTF-8, wincred as UTF-16),
  read with Meziantou.Framework.Win32.CredentialManager – the same as the GTK fork's libgit2 code. Verified against
  the user's private GitHub repo. The app never asks for credentials itself.

- InkCanvas/InkToolbar exist for WinUI 3 only in the **experimental** Windows App SDK (`Microsoft.WindowsAppSDK.WinUI`
  + `.DWrite` 2.6.6-experimental, component packages to avoid the AI/ML DLLs). The app is unpackaged and
  self-contained; `EnableMsixTooling=true` is needed so `dotnet publish` includes the app's .pri (without it the
  release build crashes at start in Microsoft.UI.Xaml.dll).
- The XAML ink types are in `Microsoft.UI.Xaml.Controls` (InkPresenter, InkStrokeContainer, InkInputProcessingMode,
  ...) and clash with `Windows.UI.Input.Inking` (InkStroke, InkPoint, InkStrokeBuilder, InkDrawingAttributes) –
  use aliases.
- `InkStrokeContainer` has no public constructor and `InkPresenter.StrokeContainer` is read-only: pages adopt the
  canvas' container. A removed stroke cannot be re-added: undo adds `Clone()`s.
- Windows ink draws a stroke of size s at pressure p about `s * (0.25 + 1.5 p)` wide (measured).
- **2.6.6-experimental raises neither `InkPresenter.UnprocessedInput` nor `StrokeInput` events** (tested in the
  sandbox with pen and mouse, mode `None` and `RightDragAction.LeaveUnprocessed`). Pen input never reaches XAML
  pointer events while a presenter takes pen input, not even with the InkCanvas' hit testing off. `StrokesCollected`
  and `StrokesErased` work. Re-test UnprocessedInput with each new Windows App SDK release.
- `InkAnalyzer` (Windows.UI.Input.Inking.Analysis) works in the unpackaged app; for circles/ellipses
  `InkAnalysisInkDrawing.Points` are the ends of both axes (0–2, 1–3).
- The pen's eraser end erased in the sandbox in the first session but not in later runs (same native setup) –
  unresolved, check on the device.
- UI Automation: the InkToolbar's own buttons have the ids `InkToolbarBallpointPenButton`,
  `InkToolbarHighlighterButton`, `InkToolbarEraserButton` (their `x:Name`/`AutomationProperties.Name` is
  ignored); custom buttons and other controls use their `x:Name`.
- `ScrollView` needs `ContentOrientation="Vertical"` (with `None`, zoomed content was clipped at its unzoomed width).
- `PdfPage.Size` is in DIPs (points × 96/72); `PdfPageRenderOptions.SourceRect` too.
- .xopp: highlighter alpha 0x7f; only the first PDF background carries `domain`/`filename`; `pageno` may look
  like "3ll" in old files; copied PDF areas of other builds are `<teximage>` with a one-page PDF as base64.
- Ink ignores posted window messages: UI tests need real pointer input (`InjectSyntheticPointerInput`).

## Testing

- Unit tests: `XournalStudio.Tests` compiles `Model/` directly (no app, no XAML).
- UI test: `UiTests/Run-SandboxTest.ps1 -Document examples\Schaltungen.xopp` runs the installed app in
  Windows Sandbox (the sandbox window opens on the desktop – tell the user first) with synthetic pen/touch input,
  screenshots per step, and checks the saved file with the GTK Xournal Studio CLI (`--create-img`).
  Inside the sandbox: PowerShell 5.1 (Add-Type = C# 5), 1514x900 at 150 %.
- Test hooks: `XOURNAL_STUDIO_CONFIG_DIR`, `XOURNAL_STUDIO_LOG`, `XOURNAL_STUDIO_TEST_PAGE`.
- `examples/` holds sanitized real documents; never commit the user's own files (they contain university
  material and personal data) – derive examples with `tools/make_examples.py` and check each page visually.

## Open work (as of 2026-11-15; update when done)

1. **Git explorer – done 2026-11-15** (not yet tried by the user on the device; sync not run against the real
   repo by Claude). Possible next steps if asked: folder status colors, rename/delete files, new document in a folder.
2. **Lasso, shape tools (line, arrow, rectangle, ellipse), copying PDF areas.** Decided to wait until the Windows
   App SDK raises `InkPresenter.UnprocessedInput` (see Facts learned); re-test with each new release. Then: lasso
   via `SelectWithPolyLine` + barrel button (`RightDragAction.LeaveUnprocessed`), copy area = render the rectangle
   with `PdfPageRenderOptions.SourceRect` as PNG into a plain `<image>`. Until then parts of an exercise come in via
   the Snipping Tool + paste. Pasted images can not be moved yet (needs the lasso).
3. **Pen eraser end** did not erase in later sandbox runs (worked in the first one, native code only) – the user
   should check it on the device; if it fails there too, investigate before anything else.
4. **Device test by the user** of everything: latency, pressure, palm rejection, touch scroll/zoom, the pen's
   buttons (Lenovo barrel button may arrive as right click).
5. Not started, only if the user asks: text, inserting pages of another PDF, PDF export, page reordering.
