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
  previous tool. "Hold barrel = lasso" in one stroke needs readable `UnprocessedInput` (PR #12292).
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
  `ExplorerView` + `ExplorerItem` (TreeView of the project folder in the SplitView pane, git colors incl. folders,
  sync button; context menus `ItemMenu`/`FolderMenu` and F2/Del/Ctrl+Shift+C on the tree raise `CommandRequested`,
  which `MainWindow` carries out because the open document may be affected; "Im Explorer anzeigen" = WinRT
  `Launcher.LaunchFolderPathAsync` with `ItemsToSelect`, "Als Pfad kopieren" = quoted path like File Explorer;
  a `FolderWatcher` change restarts a 400 ms timer, then the window refreshes the tree).
- `Services/` – `UndoStack` of `IPageEdit`: `PageEdit` (strokes/pictures removed/added) and `MoveEdit` (the
  InkCanvas has no undo; strokes are resolved through `XoppPage.Current` because undo/redo re-adds copies),
  `ShapeRecognition` (InkAnalyzer result → clean stroke), `PdfBackground`, `PictureRenderer`,
  `ClipboardService` (bitmap ↔ `<image>`; own format `XournalStudio.Image` keeps the size in points), `AppSettings`, `DebugLog`, `ProjectTree` (which files the explorer
  shows; empty folders are shown, folders with only non-documents not), `GitSync` (LibGit2Sharp: status,
  `WithFolders` marks folders by what is below them – modified/deleted > new; sync = commit all, fetch,
  rebase/fast-forward, push; conflicts are aborted), `GitCredentials` (reads Git for Windows' stored credential, see
  Facts learned), `ProjectFiles` (new folder, rename, delete to the recycle bin with `StorageFile`/`StorageFolder`;
  a .xopp's upstream attached PDF `<name>.xopp.bg.pdf` goes along; name validation, "Name (2)" like File Explorer),
  `FolderWatcher` (`FileSystemWatcher` on the project folder, hidden folders such as .git ignored).

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
- **`InkPresenter.UnprocessedInput` and `StrokeInput` ARE raised in 2.6.6-experimental** (mode `None` and
  barrel + `RightDragAction.LeaveUnprocessed`: pressed, 16× moved, released, on the UI thread), **but their args are
  `Windows.UI.Core.PointerEventArgs` whose `CurrentPoint` and `GetIntermediatePoints()` throw 0x80004021
  CO_E_NOT_SUPPORTED** – no position. (An earlier note said the events never fire: wrong, the COMException inside the
  handler was swallowed and hid the log line. Log first, then touch risky members in try/catch that logs.)
  Root cause (WinUI bug, not our usage): WinUI hosts the presenter on its ink thread and forwards the OS args to the
  UI thread, but `PointerPoint` is not marshalable. Fix: microsoft/microsoft-ui-xaml PR #12292 (open, not merged as
  of 2026-11-15; reproduced with 2.5.4-experimental too). No app-side workaround exists – check the PR and re-test
  with each new Windows App SDK release.
- Pen input never reaches XAML pointer events while a presenter takes pen input, not even with the InkCanvas' hit
  testing off. With the pen removed from `InkPresenter.InputDeviceTypes`, XAML pointer events arrive with
  `PointerDeviceType.Pen` – but the `ScrollView` captures the pen for panning after ~100 ms unless it has
  `IgnoredInputKinds="Pen"`; with that, pressed/moved/released all arrive (tested 2026-11-15).
- UI tests on the user's desktop: check that the test window is in the foreground before injecting input (the user
  works in parallel); otherwise strokes go nowhere and the test lies.
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
- Explorer (2026-11-15): one `MenuFlyout` resource is shared by all rows as `ContextFlyout` (`FlyoutBase.Target` =
  the TreeViewItem, `TreeView.ItemFromContainer` gives the row). `DispatcherQueue.TryEnqueue(
  timer.Start)` (method group of a WinRT object) throws InvalidCastException while marshaling – use a lambda. A
  `FileSystemWatcher` handler runs on the thread pool: an exception there ends the app. A loaded `PdfDocument`
  (Windows.Data.Pdf) does not lock its file: it and its folder can be renamed or recycled while open.
  `StorageFile.GetFileFromPathAsync` rejects forward slashes (FileNotFoundException) – pass `Path.GetFullPath`.
- Upstream .xopp PDF reference: `domain="absolute"` + `filename` = path written by `Util::normalizeAssetPath`
  (absolute or relative to the .xopp, by the path storage mode), or `domain="attach"` + `filename="bg.pdf"` =
  `<name>.xopp.bg.pdf` next to the document (found by the document's name, so it is renamed with it).
  Upstream's default storage mode is relative (a loaded rooted path switches to absolute). We do the same: new
  documents write the PDF relative to the .xopp with '/' (portable in the git-synced folder); a loaded reference
  is written back verbatim while it still names the PDF from the save location, otherwise recomputed in its mode.
  The user's real documents all use relative paths (checked 2026-11-15).
- The debug build (HEAD before the explorer work too) sometimes crashed with 0xc000027b (stowed exception) in
  CoreMessagingXP.dll when started/closed by a script (`Start-Process`, `CloseMainWindow`) – not investigated.

## Testing

- Unit tests: `XournalStudio.Tests` compiles `Model/` and the non-UI explorer services (`ProjectTree`, `GitSync`,
  `ProjectFiles`, `FolderWatcher`, `DebugLog`) directly (no app, no XAML).
- UI test: `UiTests/Run-SandboxTest.ps1 -Document examples\Schaltungen.xopp` runs the installed app in
  Windows Sandbox (the sandbox window opens on the desktop – tell the user first) with synthetic pen/touch input,
  screenshots per step, and checks the saved file with the GTK Xournal Studio CLI (`--create-img`).
  Inside the sandbox: PowerShell 5.1 (Add-Type = C# 5), 1514x900 at 150 %.
- Test hooks: `XOURNAL_STUDIO_CONFIG_DIR`, `XOURNAL_STUDIO_LOG`, `XOURNAL_STUDIO_TEST_PAGE`.
- `examples/` holds sanitized real documents; never commit the user's own files (they contain university
  material and personal data) – derive examples with `tools/make_examples.py` and check each page visually.

## Open work (as of 2026-11-15; update when done)

1. **Git explorer – done 2026-11-15** (not yet tried by the user on the device; sync not run against the real
   repo by Claude). **File functions – done 2026-11-15**: context menu (Öffnen, Neues Dokument, Neuer Ordner,
   Umbenennen, Löschen to the recycle bin with confirmation, Im Explorer anzeigen, Als Pfad kopieren), F2/Del/
   Ctrl+Shift+C, folder git colors, tree follows changes on disk. Unit-tested; in the app only the startup and the
   watcher were checked (screenshots) – menus, dialogs, keys, recycle bin, launcher not yet tried by hand.
   Renaming a .xopp renames only it (+ its attached `.xopp.bg.pdf`): the referenced lecture PDF stays and keeps
   working, but then shows up again in the tree under its own name (pairing is by name) – user decision pending
   whether a rename should take the same-named PDF along. Not done: drag & drop moving (TreeView's built-in
   drag & drop reorders its data, it does not know files).
2. **Lasso, shapes, PDF areas, ruler – done 2026-11-15** (tested with synthetic pen on the desktop: shapes, lasso,
   live move, delete, ink copy/paste, undo, PDF area copy + paste at original size, barrel-button lasso cycle, saved
   XML checked). Open: switching to `UnprocessedInput` (and "hold barrel = lasso") once PR #12292 ships; copying pictures (only strokes are
   copied with Ctrl+C); PDF areas only from PDF pages (no ink in the copy).
3. **Pen eraser end** did not erase in later sandbox runs (worked in the first one, native code only) – the user
   should check it on the device; if it fails there too, investigate before anything else.
4. **Device test by the user** of everything: latency, pressure, palm rejection, touch scroll/zoom, the pen's
   buttons (Lenovo barrel button may arrive as right click).
5. Not started, only if the user asks: text, inserting pages of another PDF, PDF export, page reordering.
