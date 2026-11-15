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

- **Doctrine: Microsoft's APIs do the work, the app only wires them up correctly.** Every feature is built the
  way Microsoft builds it (docs, samples, the controls' own extension points): selecting, moving, deleting,
  copying ink is `InkStrokeContainer`'s job, straight lines are the Windows ruler's, PDF rendering is
  Windows.Data.Pdf's, sync is LibGit2Sharp's, and so on. App code connects input, feedback, undo and the .xopp
  mapping – nothing more. When something seems to need own logic, first look for the API that does it; when an
  API is broken, find the root cause (issue/PR) and stay as close to Microsoft's intended route as possible.
  The user wants this to be ideal: rebuilding existing code to get closer to it is welcome.

- **No home-made solutions, no hacks.** Use what exists: WinUI 3 controls, Windows features/APIs (Windows Ink,
  ink analysis, Windows.Data.Pdf, clipboard, pickers, ...) and established libraries (NuGet). Do not re-implement
  something a platform feature or library already does, and do not work around platform behavior (toggling input
  devices, hit-testing tricks, hooks, timers to fight the framework, ...). If the native way does not work, stop
  and report it to the user with the options instead of building a workaround.
- **Never swallow exceptions** (app code, event handlers, experiments). Log them with type, HResult and message;
  `App` logs unhandled/unobserved ones. Before concluding "the platform does not raise X", prove the handler ran
  with a log line at its very start.

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
- **Pen buttons:** eraser end / eraser button erases (native). Barrel button (agreed 2026-11-15): while inking, a
  barrel press switches the toolbar to the lasso (`RightDragAction.LeaveUnprocessed` + `UnprocessedInput` only
  signals it, `InkToolbar.ActiveTool` switches); in the lasso a barrel drag lassos, a barrel tap returns to the
  previous tool. "Hold barrel = lasso" in one stroke is possible with `IsPrimaryBarrelButtonInputEnabled = false`
  (barrel input then arrives as readable XAML pointer events) – awaiting the user's decision (Open work 2).
- **Decisions (2026-11-15):** eraser = native Windows ink eraser (whole strokes; no pixel eraser, Windows has
  none); shape recognition = Windows `InkAnalyzer`; straight lines also with the Windows ruler (stencil button).
  Lasso, shapes and PDF areas are `InkToolbarCustomToolButton`s built after Microsoft's pattern ("Pen interactions
  and Windows Ink": selection layer, `SelectWithPolyLine`, `MoveSelected`); because `UnprocessedInput` has no
  readable pointer data yet (PR #12292), a custom tool sets `InkPresenter.IsInputEnabled = false` (as Microsoft's
  SimpleInk sample does for a non-ink mode) and input arrives as XAML pointer events. Agreed with the user. When an
  SDK ships PR #12292: switch the input source to `UnprocessedInput` and add the barrel-button lasso.
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
  `XoppFormat` (creator, file version), `ShapeGeometry` (outlines of shapes and recognized shapes → stroke).
  Moved strokes: `XoppPage.MoveStrokes` translates their original elements (`StrokeConverter.Translate`), moved
  pictures change their attributes in place.
- `Views/` – `PageView` (background: color + ruling or PDF page rendered at the current zoom near the viewport;
  pictures layer; InkCanvas; overlay `ToolLayer` for tool feedback), `ToolController` (copies the InkToolbar's
  tool to all pages' presenters; custom tool active → presenters `IsInputEnabled = false`, mode None),
  `InkTools` (lasso/move/delete/copy, shapes, PDF area: pointer input on the page, live feedback on the overlay,
  the work done by `InkStrokeContainer`),
  `ExplorerView` + `ExplorerItem` (TreeView of the project folder in the SplitView pane, git colors, sync button).
- `Services/` – `UndoStack` of `IPageEdit`: `PageEdit` (strokes/pictures removed/added) and `MoveEdit` (the
  InkCanvas has no undo; strokes are resolved through `XoppPage.Current` because undo/redo re-adds copies),
  `ShapeRecognition` (InkAnalyzer result → clean stroke), `PdfBackground`, `PictureRenderer`,
  `ClipboardService` (bitmap ↔ `<image>`; own format `XournalStudio.Image` keeps the size in points), `AppSettings`, `DebugLog`, `ProjectTree` (which files the explorer
  shows), `GitSync` (LibGit2Sharp: status; sync = commit all, fetch, rebase/fast-forward, push; conflicts are
  aborted), `GitCredentials` (reads Git for Windows' stored credential, see Facts learned).

## Facts learned (keep)

- Custom tools: with `InkPresenter.IsInputEnabled = false` pen/mouse arrive as XAML pointer events
  (`PointerDeviceType.Pen`); the `ScrollView` needs `IgnoredInputKinds="Pen"` or it captures the pen for panning.
  `InkToolbarCustomToolButton.ConfigurationContent` gives the tool a native flyout (opens on the second tap, stays
  open until a tap outside, like the pens' flyouts).
- Clipboard: `Clipboard.Flush()` often failed with 0x800401D0 CLIPBRD_E_CANT_OPEN (another process – clipboard
  history – opens it right after `SetContent`), and after a successful flush a custom string format came back as an
  unreadable `IInspectable`. Not flushing: content stays while the app runs and the string arrives as `string`.
- `PdfPage.RenderToStreamAsync` renders `DestinationWidth/Height` ×1.5 larger at 150 % display scale (DIPs);
  harmless for copied areas (their size in points is stored separately).
- `PathIcon` fills its geometry: outline icons need even-odd rings (`F0`).
- Saving adds formatting whitespace (newlines) between elements; element content and attributes stay identical.

- **The experimental InkCanvas composes its ink above all XAML laid over it and takes pen/mouse input there**
  (cause: `IsAboveContent(true)` on the lifted compositor; a fix was merged and reverted because pointer-drawn ink
  then vanished – microsoft-ui-xaml #11904/#11928, issue #10461; still so in 2.6.6-experimental). Tested in the sandbox
  2026-11-15: **ContentDialog** – ink over the dialog, pen strokes on it/its buttons became ink; `ShowDialogAsync`
  (agreed with the user) collapses the pages' InkCanvas: no ink, the pen taps the dialog button – use it for every
  dialog (`IsInputEnabled = false` also stops the input but leaves the ink drawn over the dialog). **InfoBar laid over
  the pages** – overdrawn and inked: it now has its own row above the ScrollView; never lay XAML over a page.
  **MenuBar menus and the custom tool flyout (`ConfigurationContent`)** – fine: drawn above the ink, a pen tap on a
  menu item invokes it, no ink (windowed popups). Re-test and remove `ShowDialogAsync`'s hiding with each new
  Windows App SDK release.
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
- **`InkPresenter.UnprocessedInput` and `StrokeInput` ARE raised in 2.6.6-experimental** (mode `None` and
  barrel + `RightDragAction.LeaveUnprocessed`: pressed, 16× moved, released, on the UI thread), **but their args are
  `Windows.UI.Core.PointerEventArgs` whose `CurrentPoint` and `GetIntermediatePoints()` throw 0x80004021
  CO_E_NOT_SUPPORTED** – no position. (An earlier note said the events never fire: wrong, the COMException inside the
  handler was swallowed and hid the log line. Log first, then touch risky members in try/catch that logs.)
  Root cause (WinUI bug, not our usage): WinUI hosts the presenter on its ink thread and forwards the OS args to the
  UI thread, but `PointerPoint` is not marshalable. Fix: microsoft/microsoft-ui-xaml PR #12292 (open, not merged as
  of 2026-11-15; reproduced with 2.5.4-experimental too). No app-side workaround exists – check the PR and re-test
  with each new Windows App SDK release. 2.6.6-experimental is the newest package with InkCanvas (stable 2.6.5 has no
  ink types in its metadata); `CoreInkIndependentInputSource` is not part of the WinUI 3 ink surface (InkToolbar.cpp
  documents it as a dropped OS-internal capability).
- **`InkPresenter.InputConfiguration.IsPrimaryBarrelButtonInputEnabled = false`** (documented: "ignore barrel input",
  UWP sample ScenarioBarrelEraserIgnore) makes barrel-button input arrive as **XAML pointer events with readable
  data** while the pen tool stays active: pressed/moved/released on the page, `PointerDeviceType.Pen`,
  `IsBarrelButtonPressed = true`, `IsRightButtonPressed = true` while in contact (sandbox 2026-11-15). This allows
  "hold barrel = lasso" in one stroke today – not built yet, needs the user's decision (see Open work).
- Pen input never reaches XAML pointer events while a presenter takes pen input, not even with the InkCanvas' hit
  testing off. With the pen removed from `InkPresenter.InputDeviceTypes`, XAML pointer events arrive with
  `PointerDeviceType.Pen` – but the `ScrollView` captures the pen for panning after ~100 ms unless it has
  `IgnoredInputKinds="Pen"`; with that, pressed/moved/released all arrive (tested 2026-11-15).
- UI tests on the user's desktop: check that the test window is in the foreground before injecting input (the user
  works in parallel); otherwise strokes go nowhere and the test lies.
- `InkAnalyzer` (Windows.UI.Input.Inking.Analysis) works in the unpackaged app; for circles/ellipses
  `InkAnalysisInkDrawing.Points` are the ends of both axes (0–2, 1–3).
- The pen's eraser end works (native, `PEN_FLAG_INVERTED|PEN_FLAG_ERASER`; `INVERTED` alone draws ink). The
  earlier "did not erase" was the synthetic input: strokes had a sample every ~38 DIPs and the eraser crossed between
  two – the Windows eraser does not hit a collected stroke between far-apart samples (two-point shape strokes from
  `InkStrokeBuilder` are hit along the line). `Inj` now injects a sample every ~4 DIPs, like a real pen.
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
  Inside the sandbox: PowerShell 5.1 (Add-Type = C# 5), 1514x900 at 150 %. `-App <publish folder>` tests another
  build (never install to test), `-Test <script>` runs an experiment instead of `SandboxTest.ps1`. The tests start
  with `{"ExplorerOpen":false}` in the config dir (with the pane open strokes begin beside the page). In a test
  script never name a function `Erase` (PowerShell alias of Remove-Item).
- Test hooks: `XOURNAL_STUDIO_CONFIG_DIR`, `XOURNAL_STUDIO_LOG`, `XOURNAL_STUDIO_TEST_PAGE`.
- `examples/` holds sanitized real documents; never commit the user's own files (they contain university
  material and personal data) – derive examples with `tools/make_examples.py` and check each page visually.

## Open work (as of 2026-11-15; update when done)

1. **Git explorer – done 2026-11-15** (not yet tried by the user on the device; sync not run against the real
   repo by Claude). Possible next steps if asked: folder status colors, rename/delete files, new document in a folder.
2. **Lasso, shapes, PDF areas, ruler – done 2026-11-15** (tested with synthetic pen on the desktop: shapes, lasso,
   live move, delete, ink copy/paste, undo, PDF area copy + paste at original size, barrel-button lasso cycle, saved
   XML checked). Open: switching to `UnprocessedInput` once PR #12292 ships; copying pictures (only strokes are
   copied with Ctrl+C); PDF areas only from PDF pages (no ink in the copy).
   **Decision for the user:** "hold barrel = lasso" in one stroke works today with
   `IsPrimaryBarrelButtonInputEnabled = false` + the XAML pointer events the custom tools already use (see Facts
   learned); it would replace the barrel press → toolbar switch → barrel tap back cycle.
3. **Pen eraser end – resolved 2026-11-15:** works in the sandbox; the failures were sparse synthetic strokes (see
   Facts learned). Still part of the device test.
4. **Device test by the user** of everything: latency, pressure, palm rejection, touch scroll/zoom, the pen's
   buttons (Lenovo barrel button may arrive as right click).
5. Not started, only if the user asks: text, inserting pages of another PDF, PDF export, page reordering.
