# CLAUDE.md

Guidance for Claude Code in this repository. Keep it current: add what is learned, remove what turns out wrong.

## Current WinUI ink state (2026-11-16 evening)

- **Build:** the app compiles against the official `Microsoft.WindowsAppSDK.WinUI` + `.DWrite` **2.6.6-experimental**
  (XAML compiler, projection, metadata) and `XournalStudio.csproj` copies the **Release (fre) runtime** of the WinUI fork
  (robertkleinschuster/microsoft-ui-xaml, branch `fix/ink-unprocessedinput-pointer-snapshots` = upstream main +
  microsoft-ui-xaml PR #12292) from `.local-build/winui-fre` over the package's runtime after Build/Publish, for every
  configuration; the build fails if it is missing. `tools/Get-WinUIRuntime.ps1` fetches it (pinned URL + SHA-256) from
  the fork's release `winui-ink-fix-70a93db5` (zip of the fre payload of fork Actions run 43875126479); `install.ps1`
  and `.github/workflows/release.yml` call it. A tag `v*` builds a pre-release with the self-contained zip; a manual
  run only builds. When an official package ships PR #12292: reference it and drop the overlay and the script.
- **Never use the chk (Debug) WinUI payload:** it stops at internal asserts (`0xC0000420` STATUS_ASSERTION_FAILURE) –
  that was the whole "startup crash" before 2026-11-16 evening.
- **Never use the fork's own package (3.0.0-dev) for compiling:** the XAML compiler of upstream main emits WinUI's
  `ScrollPresenter` into the app's XamlTypeInfo as a full activatable type with only `Content` instead of a return-type
  stub (`SetIsReturnTypeStub()`); non-stub entries are not passed on to WinUI's `XamlControlsXamlMetaDataProvider`, so
  WinUI's own ScrollView template fails ("Failed to assign to property ...ScrollPresenter.CanContentRenderOutsideBounds",
  XamlParseException at start; minimal probe: a ScrollView declared in XAML crashes, one created in code works). The
  winmd metadata is identical to 2.6.6-experimental; not reported upstream yet (public, needs approval). Main also
  dropped static initializers for types without `.cctor` (157d7c9d) – harmless here.
- **Exit hang = Windows.Data.Pdf + Intel driver (debugged 2026-11-16 with WinDbg/cdb, existed with every build):**
  `PdfPage.RenderToStreamAsync` creates a D3D11 device of its own in a static (`s_pdfStaticsInstance`) that is only
  released in Windows.Data.Pdf's DLL detach at process exit; there the Intel Iris Xe driver (igd10um64xe, 32.0.101.7088)
  spun forever in the device's release (process left at 100 % of a core, only Task Manager could end it; intermittent).
  Fix after Microsoft's interop route: `Services/PdfRenderer` renders with `PdfCreateRenderer`/
  `IPdfRendererNative::RenderPageToSurface` (windows.data.pdf.interop.h) on a D3D11 device the app owns (Vortice.Direct3D11)
  and releases it on close (`MainWindow.OnClosing`); `RenderToStreamAsync` is not used any more (loading a PDF alone does
  not touch the GPU driver – probe). PdfPage/PdfDocument are agile: rendering runs on the thread pool. Debug a hang again
  with `cdb -pv -p <pid> -c ".reload; ~*kn 60; .dump /ma hang.dmp; q"` (WinDbg from winget, symbols from msdl).
- **Release snapshots (`winui3/release/*`) cannot be built outside Microsoft:** they need internal package versions
  (e.g. InteractiveExperiences 2.1.0-experimental-27200…); the restore opens Microsoft sign-in pages through the build's
  `VSS.NuGet.AuthHelper`. Never run a WinUI restore that can prompt interactively.
- **Custom tools on `InkPresenter.UnprocessedInput` (sandbox 2026-11-16, all steps pass):** custom tool → presenters
  `InputProcessingMode.None`; barrel → `RightDragAction.LeaveUnprocessed`. With PR #12292 the args are readable on the
  UI thread. **Positions are in the presenter's (InkCanvas') coordinates** = stroke and overlay coordinates – no
  transform (an extra XamlRoot→page transform put every lasso beside the page). Unprocessed input also carries
  **Entered/Hovered/Exited while the presenters ink**, with `IsInverted`: the eraser end is taken before its first
  touch (`InkTools.TakeEraserEnd`, only in range and while erasing is partial), a tip hover in range gives it back –
  partial erasing with the eraser end now works from the first touch. Handlers log exceptions themselves
  (`InkTools.Logged`): WinUI's forwarding would not hand them to `App.UnhandledException`.

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
- **Simple, flat and honest – no magic** (user, 2026-11-15). The GTK fork grew so much hidden behavior that it
  became confusing. Show things as they are and do exactly what the user asked: the explorer shows every file
  and folder (only what Windows itself marks hidden/system is left out), nothing is paired, filtered, renamed or
  deleted "along with" something else, opening a PDF does not redirect to a .xopp of the same name and does not
  pick a file name behind the user's back (the first save asks). Existing material of the GTK fork will be
  migrated later instead of being accommodated by special cases.
- **Little custom code, strictly encapsulated.** What is left (the .xopp mapping, glue between controls) lives in
  small classes with one job: `Model/` (file format, unit-tested), `Views/`, `Services/`.
- **No monster classes, clean modular code** (user, 2026-11-15). A class has one job and stays small (a few hundred
  lines at most); `MainWindow` only wires the window's controls to feature classes (document session, recovery,
  sync, explorer commands, selection, tasks) and does not grow their logic. When a class grows, split it before
  adding more. Logic without UI goes where the unit tests can reach it.
- **Iterative.** Only features in actual use, simplest version first. Ask before large new areas.
- **Windows 11 only**, this machine's version (26200) – no compatibility layers.
- **Right-handed UI:** nothing interactive at the right screen edge; one toolbar row, left-aligned; menu bar
  visible; scrollbars hidden; the left side is for the explorer. Touch never draws (it scrolls/zooms).
- **Pen buttons:** eraser end / eraser button erases (native; with partial erasing on, the app takes it on its
  inverted hover). Barrel button (user, 2026-11-15): hold barrel = lasso (always, whatever mode the Auswählen tool has
  – user, 2026-11-16), with every tool and without switching the toolbar: `RightDragAction.LeaveUnprocessed` leaves
  it to `UnprocessedInput`, `InkTools` hands it to the `SelectionTool`; a barrel drag inside the selection moves it,
  on a corner handle scales it, a barrel tap clears it (or selects the picture under it). The tip hovering over the
  selection moves/scales it too (device 2026-11-16: the user expected that), elsewhere it inks.
- **Saving only by choice, nothing lost** (user, 2026-11-15): whenever a document is left (another opened, window
  closed, file renamed/deleted in the explorer, sync) the same question Speichern / Nicht speichern / Abbrechen;
  no saving on focus loss. Unsaved changes are copied to `%LOCALAPPDATA%\XournalStudioForWindows\Recovery` (never
  next to the document: the folder is git-synced) a few seconds after a change and on focus loss; a copy of a
  session that ended without a decision is offered at the next start (Wiederherstellen / Verwerfen = recycle bin /
  Später). New documents are named like Xournal++ (`%F-Notiz-%H-%M`, its German default) or after their PDF.
- **One selection tool** (user, 2026-11-15: "Vereinheitlichung"): Auswählen = lasso or rectangle for strokes and
  pictures; a rectangle on a PDF page also marks that area – copying takes what is seen (rendered PDF + ink and
  pictures in it). Selected, pasted and duplicated content is selected the same way: move, scale (corners),
  duplicate, cut/copy, delete (buttons in the command row, enabled while something is selected – decided 2026-11-16
  instead of a flyout at the selection, see Decided).
- **Decisions (2026-11-15):** eraser = native Windows ink eraser (whole strokes; Windows has no partial eraser – the
  partial eraser is an agreed exception since 2026-11-16, see below); shape recognition = Windows `InkAnalyzer`; straight lines also with the Windows ruler (stencil button).
  Selection and shapes are `InkToolbarCustomToolButton`s built after Microsoft's pattern ("Pen interactions
  and Windows Ink": selection layer, `SelectWithPolyLine`, strokes transformed by `InkStroke.PointTransform`), fed by
  the presenters' `UnprocessedInput` (mode None for a custom tool) – see "Current WinUI ink state".
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
  Moved/scaled strokes: the ink by `PointTransform`, their original elements by `StrokeConverter.Transform`
  (`Selection.Preview` sets `PointTransform` live and registers the committed transforms with the page –
  `XoppPage.BeginPreview`, read back by `CommittedTransform`, ended by every exit of a drag – so a snapshot writes
  strokes drawn in this session unmoved; `Commit` → `XoppPage.RecordTransform`; widths stay, as Xournal;
  undo/redo restore an exact `Placement` captured before and after); pictures change their attributes in
  place. `XoppPage.ElementOf`/`AddStrokeElement` copy strokes as their elements (exact widths, unknown attributes).
  `XoppClip` (a copied group relative to its corner; clipboard only), `TodoTxt` (todo.txt lines, doc: links),
  `SelectMode`. `XoppDocument.Snapshot` (detached XML, PDF references as from a given base) + `Write` (any thread)
  serve saving and recovery copies; `SuggestedName`/`DefaultName` = Xournal's date-time name.
- `MainWindow` only wires menus, toolbar and panes to the feature classes below (no feature logic).
- `Views/` – `PageView` (background: color + ruling or PDF page rendered at the current zoom near the viewport;
  pictures layer; InkCanvas; overlay `ToolLayer` for tool feedback), `PagesController` (page views in the
  ScrollView, zoom, page in view, insert/remove pages, PDF rendering near the viewport), `DocumentSession` (open,
  save, save-as with the suggested name/folder, the leave question, recovery offer and `Autosave`),
  `RecoveryPrompt`, `SyncController`, `ExplorerCommands` (what the explorer asks for), `Dialogs` (all
  ContentDialogs; hides the ink while one is open), `Messages` (InfoBar), `ToolController` (copies the InkToolbar's
  tool to all pages' presenters; processing mode None for a custom tool and for hover takeovers – only the mode is
  switched then),
  `InkTools` (routes the presenters' unprocessed pen/mouse input to `SelectionTool`, `ShapeTool`, `PartialEraser`;
  barrel → selection; hover takeovers of the eraser end and of the tip over the selection),
  `Selection` (selected strokes/pictures, frame + corner handles, live preview, commit as `TransformEdit`),
  `EditCommands` (cut/copy/paste/duplicate/delete/select all; pasted content placed at the left margin below the
  page's content and selected), `SelectionCommands` (enables the selection's buttons in the command row), `TasksView` + `TaskItem` (todo.txt pane), `PartialEraser`
  (circle cursor, cuts with `Model/StrokeEraser`, one undo step per drag), `PdfExportCommand` (picker, never over the
  background PDF),
  `ExplorerView` + `ExplorerItem` (TreeView of the project folder in the SplitView pane, git colors incl. folders,
  sync button; context menus `ItemMenu`/`FolderMenu` and F2/Del/Ctrl+Shift+C on the tree raise `CommandRequested`,
  which `MainWindow` carries out because the open document may be affected; "Im Explorer anzeigen" = WinRT
  `Launcher.LaunchFolderPathAsync` with `ItemsToSelect`, "Als Pfad kopieren" = quoted path like File Explorer;
  a `FolderWatcher` change restarts a 400 ms timer, then the window refreshes the tree).
- `Services/` – `UndoStack` of `IPageEdit`: `PageEdit` (strokes/pictures removed/added) and `TransformEdit` (the
  InkCanvas has no undo; strokes are resolved through `XoppPage.CurrentAll` because undo/redo re-adds copies),
  `ShapeRecognition` (InkAnalyzer result → clean stroke), `PdfBackground`, `PdfRenderer` (Windows.Data.Pdf on the
  app's own D3D device, see "Current WinUI ink state"), `PictureRenderer`,
  `ClipboardService` (own format `XournalStudio.Clip` = a `XoppClip`, plus a bitmap for other apps; bitmaps of
  other apps paste as pictures, their ink (Windows ink's format) as strokes; copies are always the exact elements), `Recovery` (recovery copies +
  info JSON with the writing process id/start time; abandoned = that process is gone), `Autosave` (timer + ordered
  write/delete queue), `TaskFile` (todo.txt line edits, re-read before each change), `PdfExport` (PDFsharp: imported PDF pages as
  vectors, `Model/BackgroundRuling` shared with the screen, strokes per segment), `AppSettings`, `DebugLog`, `ProjectTree` (the folder as it is on
  disk: all files and folders, Windows-hidden/system items left out, links shown but not entered), `GitSync` (LibGit2Sharp: status,
  `WithFolders` marks folders by what is below them – modified/deleted > new; sync = commit all, fetch,
  rebase/fast-forward, push; conflicts are aborted), `GitCredentials` (reads Git for Windows' stored credential, see
  Facts learned), `ProjectFiles` (new folder, rename, delete to the recycle bin with `StorageFile`/`StorageFolder`,
  exactly the item given; name validation, "Name (2)" like File Explorer),
  `FolderWatcher` (`FileSystemWatcher` on the project folder, hidden folders such as .git ignored).

## Facts learned (keep)

- The `ScrollView` keeps `IgnoredInputKinds="Pen"`: the pen never pans (it would capture the pen otherwise).
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
- **Unprocessed input (with PR #12292, device and probe 2026-11-16):** positions are InkCanvas coordinates; Entered/
  Hovered/Exited arrive also while the presenters ink, with `IsInverted` (eraser end and Lenovo eraser button: the
  button press makes the pen re-enter as a new pointer, `inv=1`; the touch then has `IsEraser`); barrel = `barrel=1
  right=1 kind=RightButtonPressed`. Switching only `InputProcessingConfiguration.Mode` during a hover is fine – the
  press then arrives unprocessed. Rewriting `InputDeviceTypes`/`UpdateDefaultDrawingAttributes` or having
  `IsEraserInputEnabled = false` made the presenter drop the hovering pointer (Exited in range, no press). With the
  stock 2.6.6-experimental runtime the args threw 0x80004021 CO_E_NOT_SUPPORTED (WinUI forwarded the OS args from its
  ink thread; `PointerPoint` is not marshalable) – that is what PR #12292 fixes. Probe app for such questions:
  `.local-build/probe-pen` (not committed; InkCanvas + log + switches, the user triggers pen events).
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

- **`InkStroke.PointTransform` set on a stroke in the canvas' container is redrawn live** (sandbox 2026-11-15):
  moving and scaling selections preview through it; the pen width is not scaled (the drawn width stays, matching
  Xournal's file where only the points change). Windows ink's clipboard (`CopySelectedToClipboard`/`PasteFromClipboard`)
  carries the transform: a scaled stroke pastes at its scaled size (sandbox step 03g2).
- **TreeView crash** (0xC0000005/0xC0000374): replacing the tree's items while it still had an item of the old list
  selected, then selecting the new one, crashed WinUI when the explorer pane was closed (no containers). Also on main
  before 2026-11-15; reproduced locally without input (a new subfolder while the pane was closed). Fix: deselect
  before replacing `ItemsSource`. Lists in this app are given new `ItemsSource` lists instead of Clear/Add on a
  bound `ObservableCollection` (the tasks list did crash once that way in a run that also rebuilt the tree – not
  isolated).
- `CommandBarFlyout` with `ShouldConstrainToRootBounds = false`, `ShowMode = Transient` and
  `OverlayInputPassThroughElement` = the window's root: drawn above the ink, a pen tap beside it reaches the page.
  It stays open over ContentDialogs. **But while it is open, the release of a barrel drag (pen + right button)
  reaches no XAML element** (sandbox 2026-11-16, synthetic pen): not the page's surface (`CapturePointer` returned
  true), not the window root with `handledEventsToo`, no PointerCaptureLost/Canceled; pen-tip releases arrive, and
  without the flyout the barrel release arrives. Hiding it at the press or at the first move does not help (the
  windowed popup has the pointer from the press). So a barrel move was only previewed, never committed. Not used any
  more for the selection (see Decided); never lay a windowed popup over the page where the barrel drags.
- Accelerators of the menu (Ctrl+A/C/V/X/D, Del) do not fire while a TextBox has focus (sandbox: Ctrl+A, Del in the
  task box edited the text, the page selection stayed).
- `RegisterApplicationRestart` (kernel32) is registered at start: Windows restarts the app after a crash (after
  60 s of running) or an update restart; the start then offers the recovery copy. Not tested (needs a real crash).
- Windows Sandbox: a second `wsb start` right after `wsb stop` fails with CO_E_APPSINGLEUSE – wait until the old
  sandbox process is gone.
- `dotnet test XournalStudio.Tests` crashed the test host twice on 2026-11-16 ("Der Testhostprozess ist abgestürzt",
  "Fatal error", no stack; 32/33 of 73 tests ran), both times the first run right after a build; every repeat ran all
  73, and `--blame-crash` did not reproduce it. Not investigated further – if it recurs, keep the `--blame-crash` dump.

## Testing

- Unit tests: `XournalStudio.Tests` compiles `Model/` and the non-UI services (`ProjectTree`, `GitSync`,
  `ProjectFiles`, `FolderWatcher`, `DebugLog`, `Recovery`, `AppSettings`, `TaskFile`) directly (no app, no XAML).
- UI test: `UiTests/Run-SandboxTest.ps1 -Document examples\Schaltungen.xopp` runs the installed app in
  Windows Sandbox (the sandbox window opens on the desktop – tell the user first) with synthetic pen/touch input,
  screenshots per step, and checks the saved file with the GTK Xournal Studio CLI (`--create-img`).
  Inside the sandbox: PowerShell 5.1 (Add-Type = C# 5), 1514x900 at 150 %. `-App <publish folder>` tests another
  build (never install to test), `-Test <script>` runs an experiment instead of `SandboxTest.ps1`. The tests start
  with `{"ExplorerOpen":false,"ProjectFolder":"C:\\work"}` in the config dir (with the pane open strokes begin
  beside the page) and a todo.txt in C:\work. Covered: eraser, barrel select/move, selection tool (lasso, scale,
  undo), shapes, rectangle on the PDF page → copy → new page → paste (selected) → scale → duplicate → tap-select →
  delete → undo, bitmap paste, tasks (add, keys in the text box, check), the save question on close, crash →
  recovery offered → restored → "Nicht speichern" removes the copy. Coordinates are client DIPs: pixel = 34 + 1.5 *
  DIP (y), 1.5 * DIP (x); an InfoBar above the pages shifts them. Clicking the already active pen button opens its
  flyout (the next stroke only closes it). In a test script never name a function `Erase` (PowerShell alias of
  Remove-Item); write scripts with the Write tool (shell heredocs turned `\t` in paths into tabs).
- Test hooks: `XOURNAL_STUDIO_CONFIG_DIR`, `XOURNAL_STUDIO_LOG`, `XOURNAL_STUDIO_TEST_PAGE`.
- `examples/` holds sanitized real documents; never commit the user's own files (they contain university
  material and personal data) – derive examples with `tools/make_examples.py` and check each page visually.

## Open work (as of 2026-11-15; update when done)

1. **Git explorer, file functions – done 2026-11-15** (not yet tried by the user on the device; sync not run against
   the real repo by Claude). Context menu, F2/Del/Ctrl+Shift+C, folder git colors, tree follows changes on disk.
   Flat (see constraints): rename/delete act on exactly one item; files other than .xopp/.pdf open in their Windows
   app; renaming the open document's PDF makes the document refer to the new name. Not done: drag & drop moving.
2. **Selection tool, shapes, ruler – done 2026-11-15**, on `UnprocessedInput` since 2026-11-16 evening (sandbox and
   device: lasso, barrel/tip move). Open: a tap selects pictures only (not strokes).
3. **Saving by choice + crash recovery – done 2026-11-15** (sandbox: question on close, crash → offer → restore →
   discard). Application Restart after a real crash/update not tested.
4. **Tasks (todo.txt) – done 2026-11-15** (sandbox: add, check; unit tests for format and file edits).
5. **Device test by the user** of everything: latency, pressure, palm rejection, touch scroll/zoom, the selection
   tool with pen and finger (barrel move!), the selection's commands in the command row, the tasks pane. The pen's buttons work on the device
   (eraser end, barrel lasso – confirmed by the user 2026-11-15).
6. **PDF export – done 2026-11-16** (Datei → Als PDF exportieren, Ctrl+E; `PdfExport` with PDFsharp, checked
   visually with both examples). **Partial eraser – done 2026-11-16** (see Decided). **Showing every upstream
   element – in progress 2026-11-16** (user: "zumindest in der Anzeige sollten wir xopp vollständig reproduzieren"):
   Done: texts and links, image/text `matrix`, all backgrounds (rulings with `config`, pixmap), filled/dashed/butt- or
   square-ended strokes (shown and exported from the file by `PageView.ShowDrawnStrokes`/`PdfExport`, not editable as
   ink; a filled highlighter's line over its filling comes out a little stronger – XAML and PDFsharp have no group
   opacity here, a Canvas' Opacity + BitmapCache did not group). Not supported: upstream's newer zip container format.
7. **Night 2026-11-16, reviews 9–55:** display of every upstream element, picture/text matrices, all backgrounds,
   styled strokes; the selection's commands moved to the command row (barrel release, see Decided); snapshots
   consistent during a drag; gestures belong to their pointer and begin only with a press; Save As takes attachments
   along. Merged into main 2026-11-16 morning (user: "du kannst gerne mergen"). Reviews were still finding edge cases
   (mostly the eraser end, now a question for the user) – not a clean round.
8. Not started, only if the user asks: inserting pages of another PDF, page reordering, a text tool.

## Decided with the user (2026-11-16 morning)

- After paste/duplicate the Auswählen tool stays active (no automatic switch back to the pen).
- Scaling: corner handles only, proportional, stroke width unchanged.
- Tasks: only `<project folder>/todo.txt`; no reminders, no date words in quick add.
- Default name `%F-Notiz-%H-%M`, a PDF's document suggests the PDF's name in its folder.
- Barrel button = always the lasso. Recovery copy 5 s after a change and on focus loss.
- "Meine behalten" after a sync changed the open document: stays unsaved until Ctrl+S.
- `examples/Schaltungen.xopp` reset. Reviews (12 rounds) converged on edge cases; `overnight` merged into main 2026-11-16 (f37018e).
- PDF export approved and done 2026-11-16. Later candidates: pages of another PDF, page reordering, a text tool.
- Still open: report the ink-over-dialog bug / comment on PR #12292 (public, needs approval).
- **Save As with attachments – as Xournal++ (user, 2026-11-16 morning):** attached PDF and picture backgrounds
  (`domain="attach"`, `<doc>.xopp.<filename>`) are copied next to the new file (`XoppDocument.CopyAttachments`, also for
  "Meine als Kopie speichern"), the file keeps `attach`, and the document refers to its own copy afterwards.
- **Selection commands in the command row (decided by Claude 2026-11-16 night, confirmed by the user 2026-11-16 morning):** the transient
  CommandBarFlyout at the selection swallowed the barrel release (Facts), so a barrel move never stuck – the agreed
  core (barrel = lasso and move) wins over the flyout. Ausschneiden/Kopieren/Duplizieren/Löschen are contextual
  AppBarButtons in the command row (`SelectionCommands`); nothing lies over the page.
  Verified in the sandbox: barrel lasso → barrel move is committed → Del deletes. Other options considered: a flyout
  inside the XAML tree (drawn under the ink), Win32 `GetPointerInfo` (app logic around a WinUI bug), a report to
  microsoft-ui-xaml (public, needs approval). The buttons stay in place and are enabled with a selection (nothing
  jumps). Edit commands, undo and redo wait while a drag runs (`SelectionTool.IsDragging`, also before the first move) and
  say "Erst loslassen". Saving, recovery copies and export never wait: a previewed move is registered with the page
  (`XoppPage.BeginPreview`/`EndPreview`), which writes such strokes with their committed transform – a snapshot is
  consistent at any moment. The row's buttons take no keyboard focus (`AllowFocusOnInteraction="False"`).
- **Eraser end with partial erasing (resolved 2026-11-16 evening):** taken on its inverted hover, so it erases
  partially from its first touch; the earlier "first touch only arms it" compromise is gone (a native erase before the
  hover was seen is still put back as a fallback, PagesController). WPF had a point eraser natively; WinRT/WinUI ink
  never had one (issue #1530).
- **Questions for the user (review findings kept deliberately, 2026-11-16):**
  - A picture background that fails while being drawn into the PDF is counted as "ohne Hintergrund" instead of
    failing the export (deliberate: one damaged picture should not stop the export).
  - A "clone" picture background remembers its source page by reference; a future page reordering must keep it.
- **Partial eraser approved (user, 2026-11-16)** as a deliberate exception (Windows ink erases whole strokes only):
  a toggle next to the eraser switches the eraser (tool and the pen's eraser end) between whole strokes (native) and
  partial, without changing the active tool. Partial: the radius is exact and shown as a circle at the pen (hover and
  while erasing) – exactly what is inside the circle disappears, nothing else (cut where the ink's edge, i.e. the
  centerline plus half the stroke width, meets the circle). **Done 2026-11-16.** The eraser tool erases partially
  through the app (presenters in mode None); the eraser end/button with the pen tool active is taken on its inverted
  hover (see above). User: "wichtig ist dass er robust funktioniert". Radius: context menu of the toggle (1–20 pt,
  default 4).
- **PDF export:** PDFsharp pinned to 6.2.4, the last MIT version (6.5.0 is AGPL-3.0 with license acceptance – a
  license change for this project is the user's decision).
