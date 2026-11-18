# CLAUDE.md

Guidance for Claude Code in this repository. Keep it current: add what is learned, remove what turns out wrong.

## Current WinUI ink state (2026-11-16 evening)

- **InkPages is its own repository since 2026-11-18** (user: so the MatheTrainer can use it too):
  https://github.com/robertkleinschuster/InkPages (public, history of the library's files kept), embedded here as the
  git submodule `external/InkPages` (`git submodule update --init`; `install.ps1` does it, CI checks out with
  `submodules: true`). The library, its demo, its UI tests and unit tests (`Model/`), the sandbox kit
  (`UiTests/TestKit.ps1`, `Inj.cs`) and the WinUI fork runtime (targets, `Get-WinUIRuntime.ps1`,
  `Build-WinUILocal.ps1`) live there; its CLAUDE.md and README document them. Library changes are made, committed and
  pushed in `external/InkPages` (or D:\src\InkPages), then the submodule pointer is committed here. Paths below that
  name `InkPages/`, `InkPages.Demo/`, `build/`, `tools/Get-WinUIRuntime.ps1` or `UiTests/TestKit.ps1` now mean the
  submodule's files.
- **Build:** the app compiles against the official `Microsoft.WindowsAppSDK.WinUI` + `.DWrite` **2.6.6-experimental**
  (XAML compiler, projection, metadata) and `XournalStudio.csproj` copies the **Release (fre) runtime** of the WinUI fork
  (robertkleinschuster/microsoft-ui-xaml, branch `fix/ink-runtime-highlighter` = upstream main of 2026-11-16 +
  microsoft-ui-xaml PR #12292 + the fork's InkToolbar custom tool states fix + its pointer-capture-across-routing fix +
  its custom pen drawing attributes fix, see Facts) from `external/InkPages/.local-build/winui-fre`
  over the package's runtime, for every configuration; the build fails if it is missing.
  `external/InkPages/tools/Get-WinUIRuntime.ps1` fetches it (pinned URL + SHA-256) from the fork's release `winui-ink-fix-d3b1bf22` (zip
  of the fre payload of fork Actions run 44272379119, started by `gh workflow run winui-source-product.yml --ref
  <branch>`; the release zip holds `runtimes/win10-x64/native/**`); `install.ps1`
  and `.github/workflows/release.yml` call it. A tag `v*` builds a pre-release with the self-contained zip; a manual
  run only builds. When an official package ships PR #12292: reference it and drop the overlay and the script.
- **The overlay must replace the payload before the app's .pri is generated** (2026-11-18): WinUI's control templates
  live in `Microsoft.UI.Xaml.Controls.pri`, and the app's own .pri (which merges the referenced .pri files at build
  time) is what the app reads – copying the fork's files over the output only after Build/Publish left the package's
  templates in the app (the InkToolbar fix did not show). `build/WinUIForkRuntime.targets` swaps the Windows App SDK
  payload items (`None`/`ReferenceCopyLocalPaths` with a `Link`) for the fork's files right after
  `AddMicrosoftWindowsAppSDKPayloadFilesFromComponents`; check with a search for a new template string in
  `<App>.pri`. Before this, only the fork's C++ changes (PR #12292) reached the app, not its XAML.
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
  Pixels come back with `SoftwareBitmap.CreateCopyFromSurfaceAsync` (surface wrapped by
  `CreateDirect3D11SurfaceFromDXGISurface`): it reads back on the app's device and creates no device of its own (cdb
  breakpoint on D3D11CreateDevice, 2026-11-17); the earlier staging-map → `byte[]` → `AsBuffer` copy left 11–55 MB of
  pinned garbage per page (headless bench: working set 7.8 GB after 80 renders at 3200 px, now ~0.6 GB). Per-page time is
  dominated by `RenderPageToSurface` itself (~30–40 ms at 1440 px, ~100–120 ms at 3200 px for a text+vector A4 page on
  Iris Xe). Waiting render requests are cancellable (`PageView` drops them when the page leaves the viewport, another
  width is wanted or the document changes) and pages in view are requested first.
- **Release snapshots (`winui3/release/*`) cannot be built outside Microsoft:** they need internal package versions
  (e.g. InteractiveExperiences 2.1.0-experimental-27200…); the restore opens Microsoft sign-in pages through the build's
  `VSS.NuGet.AuthHelper`. Never run a WinUI restore that can prompt interactively.

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

- **Moving/scaling the selection is XAML pointer input (`SelectionDrag`, 2026-11-17):** an app interaction, not ink –
  Microsoft's docs build only the lasso from UnprocessedInput, and their samples take the pen from the ink for a non-ink
  mode (SimpleInk Scenario9: `IsInputEnabled = false`). While the pen (tip or barrel) hovers over the selection, the
  presenters get `InputDeviceTypes = None` (`ToolController.SelectionTakenOver`, set from the presenter hover by
  `InkTools` and from the XAML hover by `SelectionDrag`); press, capture and moves then arrive on the page's `Surface`
  (Sheet) and `GetCurrentPoint(page.InkCanvas)` is right on any page, also while the view scrolls. Reason (measured in
  the sandbox with a barrel drag held at the view's edge, now in `InkPages.Demo/UiTests/ToolsTest.ps1`): the presenter that got the press keeps the pen, and its coordinates
  drift once its page scrolls out of view (lifted composition, not WinUI code) – the dragged content slid away from
  the pen. Away from the selection (XAML hover) the presenters get the pen back before it touches; it inks normally.
  The takeover needs the pen's hover before the touch (a real pen always hovers; a test must hover ~100 ms).
  Auto-scroll runs per frame (`CompositionTarget.Rendering`): scroll by speed × frame time and hand the drag the pen's
  place via XAML's transform (current for the frame). Lasso, eraser and shapes stay on UnprocessedInput.
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
git submodule update --init                # InkPages (external/InkPages)
external\InkPages\tools\Get-WinUIRuntime.ps1   # the WinUI fork runtime the build needs
dotnet build XournalStudio                 # debug build (no Visual Studio needed, .NET 10 SDK)
dotnet test XournalStudio.Tests            # unit tests (file format)
.\install.ps1                              # tests + publish + install to %LOCALAPPDATA%\Programs\Xournal Studio for Windows
```

Workflow: after a change commit atomically, push and install (`install.ps1` refuses while the installed app runs).

## Architecture

- `external/InkPages/InkPages/` (library in its own repository, git submodule; namespace `InkPages`, no app
  dependencies; the writing area): `InkWorkspace` (the facade:
  wires toolbar, tools, input, auto-scroll and undo; `Attach(page)`, `SetPages` (the pages that have a view now),
  `Holds(page)` (the page's view must stay: history, selection, running gesture or drag hand-over, shape recognition),
  `Reset`, `Removing/Removed`, `UndoStep`; the toolbar's controls hold the tools' choices), `InkPage` (abstract page:
  `UnderInk` layers of the app, InkCanvas, `Overlay`, `Surface`; the app implements its content – stroke
  identity/preview/transform, placements, pictures as the app's objects, removal tokens; `CreatePlaceholder` = a blank
  sheet for a page without view), `PenToolbar` (+ `PenToolbarTexts`, `BallpointPen`/`HighlighterPen`,
  `Extensions` for app buttons, `PenToolbar.Command`), `ToolController` (copies the InkToolbar's tool to all pages'
  presenters – drawing attributes only to pages new in `SetPages`, devices/mode only where they differ; processing mode
  None for a custom tool and for hover takeovers – only the mode is switched then),
  `InkTools` (routes the presenters' unprocessed pen/mouse input to `SelectionTool`, `ShapeTool`, `PartialEraser`;
  barrel → selection; hover takeovers of the eraser end and of the tip over the selection), `Selection` (selected
  strokes/pictures, frame + corner handles, live preview, commit as `TransformEdit`), `SelectionDrag` (XAML pointer
  move/scale), `DragAutoScroll`, `SelectionCommands` (enables the selection's buttons), `PartialEraser` (circle cursor,
  cuts with `Model/StrokeEraser`, one undo step per drag), `UndoStack` of `IPageEdit` (`PageEdit`, `TransformEdit`,
  `CombinedEdit`; the InkCanvas has no undo; undo/redo re-add copies, resolved by `InkPage.CurrentStrokes`),
  `ShapeRecognition` (InkAnalyzer → clean stroke), `InkLog` (the app routes it to `DebugLog`), `Model/` (no UI;
  compiled into the unit tests: `StrokeEraser`, `ShapeGeometry`/`ShapeKind`, `InkStrokes` (drawn width, strokes from
  samples), `SelectMode`). See open work 9.
- `Model/` – `XoppDocument` (gzip XML; the XML tree is kept as loaded, pages are taken out and put back on save;
  save writes `<file>~` then replaces), `XoppPage` (page element; strokes live in the page's InkStrokeContainer while
  it has a view – `ReadStrokes` (any thread) + `AttachInk`; `DetachInk`/`WriteBack` write the ink into the element as a
  snapshot would when the view is released; without view a page is only XML –
  unchanged strokes are written back as their original elements, mapped by ink stroke id; pictures stay XML),
  `StrokeConverter` (Xournal per-segment widths ↔ ink pressure), `XoppImage` (`<image>`/`<teximage>`),
  `XoppFormat` (creator, file version).
  Moved/scaled strokes: the ink by `PointTransform`, their original elements by `StrokeConverter.Transform`
  (`Selection.Preview` sets `PointTransform` live and registers the committed transforms with the page –
  `XoppPage.BeginPreview`, read back by `CommittedTransform`, ended by every exit of a drag – so a snapshot writes
  strokes drawn in this session unmoved; `Commit` → `XoppPage.RecordTransform`; widths stay, as Xournal;
  undo/redo restore an exact `Placement` captured before and after); pictures change their attributes in
  place. `XoppPage.ElementOf`/`AddStrokeElement` copy strokes as their elements (exact widths, unknown attributes).
  `XoppClip` (a copied group relative to its corner; clipboard only), `TodoTxt` (todo.txt lines, doc: links).
  `XoppDocument.Snapshot` (detached XML, PDF references as from a given base) + `Write` (any thread)
  serve saving and recovery copies; `SuggestedName`/`DefaultName` = Xournal's date-time name.
- `MainWindow` only wires menus, toolbar and panes to the feature classes below (no feature logic).
- `Views/` – `AppToolbar` (the library's `PenToolbarTexts.German` with this app's selection tip, `XournalPalette`, the user's `ToolChoices` from `AppSettings`, kept on `InkWorkspace.ChoicesChanged`), `PageView` (the library's `InkPage` for a
  `XoppPage`: background color + ruling or PDF page rendered at the current zoom near the viewport, pictures, texts,
  drawn strokes; implements the page content over `XoppPage`; `Close` when released), `PageSlots` (lazy page views:
  one child of the StackPanel per page – its `PageView` near the viewport, else a placeholder of its size; see Facts),
  `PagesController` (the ScrollView: zoom, page in view (`CurrentPageView()` creates its view), insert/remove pages,
  which pages have views and PDF rendering near the viewport, every 120 ms while the view moves), `DocumentSession` (open,
  save, save-as with the suggested name/folder, the leave question, recovery offer and `Autosave`),
  `RecoveryPrompt`, `SyncController`, `ExplorerCommands` (what the explorer asks for), `Dialogs` (all
  ContentDialogs; hides the ink while one is open – `PagesController.InkHidden` = the library's `InkWorkspace.InkHidden`, also on views created meanwhile), `Messages` (InfoBar),
  `EditCommands` (cut/copy/paste/duplicate of the file's elements; delete, select all, pasting Windows ink and selecting what was added through the library's `InkCommands`; pasted content placed at the left margin below the
  page's content and selected; moves a dragged selection's elements to another page), `TasksView` + `TaskItem`
  (todo.txt pane), `PdfExportCommand` (picker, never over the background PDF), `PdfImportCommand` (iPad import),
  `ExplorerView` + `ExplorerItem` (TreeView of the project folder in the SplitView pane, git colors incl. folders,
  sync button; context menus `ItemMenu`/`FolderMenu` and F2/Del/Ctrl+Shift+C on the tree raise `CommandRequested`,
  which `MainWindow` carries out because the open document may be affected; "Im Explorer anzeigen" = WinRT
  `Launcher.LaunchFolderPathAsync` with `ItemsToSelect`, "Als Pfad kopieren" = quoted path like File Explorer;
  a `FolderWatcher` change restarts a 400 ms timer, then the window refreshes the tree).
- `Services/` – `PdfBackground`, `PdfRenderer` (Windows.Data.Pdf on the
  app's own D3D device, see "Current WinUI ink state"), `PictureRenderer`,
  `ClipboardService` (own format `XournalStudio.Clip` = a `XoppClip`, plus a bitmap for other apps; bitmaps of
  other apps paste as pictures, their ink (Windows ink's format) as strokes; copies are always the exact elements), `Recovery` (recovery copies +
  info JSON with the writing process id/start time; abandoned = that process is gone), `Autosave` (timer + ordered
  write/delete queue), `TaskFile` (todo.txt line edits, re-read before each change), `PdfExport` (PDFsharp: imported PDF pages as
  vectors, `Model/BackgroundRuling` shared with the screen, strokes per segment), `AppSettings`, `DebugLog`, `ProjectTree` (sorted by name, numbers by value: `NameOrder`; the folder as it is on
  disk: all files and folders, Windows-hidden/system items left out, links shown but not entered), `GitSync` (LibGit2Sharp: status,
  `WithFolders` marks folders by what is below them – modified/deleted > new; sync = commit all, fetch,
  rebase/fast-forward, push; conflicts are aborted), `GitCredentials` (reads Git for Windows' stored credential, see
  Facts learned), `ProjectFiles` (new folder, rename, delete to the recycle bin with `StorageFile`/`StorageFolder`,
  exactly the item given; name validation, "Name (2)" like File Explorer),
  `FolderWatcher` (`FileSystemWatcher` on the project folder, hidden folders such as .git ignored).

## Facts learned (keep)

- **The tools' facts are in InkPages (moved 2026-11-18):** everything learned about the writing area – presenter input
  and hover takeovers, the selection drag on XAML input, ink above XAML and dialogs (the app: InfoBar in its own row,
  `Dialogs` sets `InkHidden`), cross-page drags, lazy views' memory, pressure/width, the fork's fixes (highlighter,
  capture, custom tool states), the toolbar and ruler – is in `external/InkPages/CLAUDE.md` ("Facts learned"); platform
  facts in the user's global CLAUDE.md. Below only what is this app's own.
- **Lazy page views (2026-11-17, `PageSlots`):** only pages within one viewport height of the view have a `PageView`
  (pages in view at once, the next near one per 120 ms tick); views beyond two heights are released unless
  `InkWorkspace.Holds` them (undo/redo history, selection, running gesture or a drag's hand-overs, shape recognition) –
  so an edit never refers to a released view: `InkStrokeContainer` has no public constructor and a stroke belongs to one
  container, so edits cannot outlive their canvas. Memory is in the `InkStroke` objects, not the canvases (sandbox,
  40-page document with 26.5k strokes: all strokes converted at load cost ~230 MB, all views only ~85 MB more), so a page
  without view holds no ink objects: its strokes are read on the thread pool when it comes near (`XoppPage.ReadStrokes`;
  nothing writes a page's element while it has no view) and written back into its element on release
  (`XoppPage.WriteBack`, exactly what a snapshot writes). Released views' event handlers come off
  (`InkWorkspace.SetPages`: the presenter's events would keep the view alive). Placeholders (`InkPage.CreatePlaceholder`)
  keep the StackPanel's layout, so `OtherPageAt`/`ActualOffset`, scrolling to a page and auto-scroll are unchanged; a
  hand-over waits until the page under the pen has its view (it is in view, so within a tick). ItemsRepeater was not
  used: it recycles/clears elements by itself, cannot keep a page with history alive, and estimates sizes.
  Measured (sandbox, same document, main → lazy): open 502 → 195 MB, UI blocked 1.8 s → 0.45 s (longest block 1.45 s →
  0.25–0.35 s); a scroll through all 40 pages: longest block 48 ms, memory stays ~250 MB; a view takes ~20 ms (max ~75).
- `ScrollView.ZoomTo` is asynchronous: restoring a document's offset right after it (an enqueued `ScrollTo`) was lost
  once opening became fast; `PagesController.RestorePosition` scrolls on `ZoomCompleted` of its request (correlation id).

- Clipboard: `Clipboard.Flush()` often failed with 0x800401D0 CLIPBRD_E_CANT_OPEN (another process – clipboard
  history – opens it right after `SetContent`), and after a successful flush a custom string format came back as an
  unreadable `IInspectable`. Not flushing: content stays while the app runs and the string arrives as `string`.
- `PdfPage.RenderToStreamAsync` renders `DestinationWidth/Height` ×1.5 larger at 150 % display scale (DIPs);
  harmless for copied areas (their size in points is stored separately).
- `PathIcon` fills its geometry: outline icons need even-odd rings (`F0`).
- Saving adds formatting whitespace (newlines) between elements; element content and attributes stay identical.

- Git: LibGit2Sharp (user's choice over the git CLI). Sign-in = the credential the Git Credential Manager / wincred
  helper stores in the Windows Credential Manager (generic `git:https://host`, GCM as UTF-8, wincred as UTF-16),
  read with Meziantou.Framework.Win32.CredentialManager – the same as the GTK fork's libgit2 code. Verified against
  the user's private GitHub repo. The app never asks for credentials itself.

- UI tests on the user's desktop: check that the test window is in the foreground before injecting input (the user
  works in parallel); otherwise strokes go nowhere and the test lies.
- `PdfPage.Size` is in DIPs (points × 96/72); `PdfPageRenderOptions.SourceRect` too.
- .xopp: highlighter alpha 0x7f; only the first PDF background carries `domain`/`filename`; `pageno` may look
  like "3ll" in old files; copied PDF areas of other builds are `<teximage>` with a one-page PDF as base64.
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

- **TreeView crash** (0xC0000005/0xC0000374): replacing the tree's items while it still had an item of the old list
  selected, then selecting the new one, crashed WinUI when the explorer pane was closed (no containers). Also on main
  before 2026-11-15; reproduced locally without input (a new subfolder while the pane was closed). Fix: deselect
  before replacing `ItemsSource`. Lists in this app are given new `ItemsSource` lists instead of Clear/Add on a
  bound `ObservableCollection` (the tasks list did crash once that way in a run that also rebuilt the tree – not
  isolated).
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
- UI tests run in Windows Sandbox via `UiTests/Run-SandboxTest.ps1` (the sandbox window opens on the desktop – tell the
  user first); in-sandbox scripts may dot-source `C:\sbx\TestKit.ps1` (`external/InkPages/UiTests/TestKit.ps1`: start
  the app, find controls, `ClickInOptions` for a tool's flyout, screenshots, `Invoke-Test` writes done.txt). **The
  library's tools are tested in the demo app, in the InkPages repository** (there: `dotnet publish InkPages.Demo -c
  Release -o .local-build\demo`, then `UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test
  InkPages.Demo\UiTests\ToolsTest.ps1`); its unit tests: `dotnet test InkPages.Tests` there: stroke eraser, eraser end, barrel
  lasso/move, selection (duplicate, scale, undo), erasing exactly (mode toggle), the eraser end following the mode,
  shapes and recognition, a barrel drag across pages with auto-scroll. `ToolbarStates.ps1` screenshots every toolbar
  state light and dark (design reviews: crop/contact-sheet them; Codex CLI `codex exec -i <png>` gave useful
  reviews). The demo logs to `INKPAGES_DEMO_LOG` (TestKit sets `out\demo.log`): gesture begin/end and undo steps.
  Synthetic pen input must hover ~100 ms before a press on the selection (`HoverAt`), as a real pen does.
- App UI test: `UiTests/Run-SandboxTest.ps1 -Document examples\Schaltungen.xopp` runs the installed app in
  Windows Sandbox (the sandbox window opens on the desktop – tell the user first) with synthetic pen/touch input,
  screenshots per step, and checks the saved file with the GTK Xournal Studio CLI (`--create-img`).
  Inside the sandbox: PowerShell 5.1 (Add-Type = C# 5), 1514x900 at 150 %. `-App <publish folder>` tests another
  build (never install to test), `-Test <script>` runs an experiment instead of `SandboxTest.ps1`. The tests start
  with `{"ExplorerOpen":false,"ProjectFolder":"C:\\work"}` in the config dir (with the pane open strokes begin
  beside the page) and a todo.txt in C:\work. Covered: eraser, barrel select/move, selection tool (lasso, scale,
  undo), shapes, rectangle on the PDF page → copy → new page → paste (selected) → scale → duplicate → tap-select →
  delete → undo, bitmap paste, tasks (add, keys in the text box, check), the save question on close, crash →
  recovery offered → restored → "Nicht speichern" removes the copy (the tools themselves: the demo's tests above). Coordinates are client DIPs: pixel = 34 + 1.5 *
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
8. Not started, only if the user asks: page reordering, a text tool. (Inserting pages of another PDF: done
   2026-11-18, see Facts.)
9. **Library `InkPages` (user, 2026-11-17; own public repository since 2026-11-18, user's choice, submodule
   `external/InkPages`; the MatheTrainer is to embed it too – not switched yet):** the
   writing area becomes a reusable module other apps can embed, the toolbar a part of it (base functions in the
   library, app-specific buttons on top; Windows defaults, dense for the pen, few taps for frequent functions).
   **Stage 1 done 2026-11-17:** `InkPages/PenToolbar` (built in code, no XAML – nothing to merge into the app's .pri):
   one-tap pen presets (black, blue, red; `InkToolbarCustomPenButton` + `BallpointPen`, each with the native
   `InkToolbarPenConfigurationControl` flyout, saved as `AppSettings.Pens`), highlighter presets (yellow, green;
   `HighlighterPen`, `AppSettings.Highlighters`), eraser + its mode toggle, select, shapes (shape recognition in
   its options), ruler; undo/redo first, the selection's commands after the InkToolbar; the app adds paste, insert page and
   delete page in `Extensions` (user 2026-11-17: delete page in the row, always with a confirmation – it cannot be
   undone; `Views/AppToolbar` = German texts, named palette, presets). Toolbar design decided with the user 2026-11-17: grouped row (undo | pens | highlighters | erasers |
   tools | selection | paste | page), two highlighter presets; the eraser's mode is a toggle (user 2026-11-17, see
   Decided: the pen's eraser button must not need a tool change; `InkWorkspace.PartialErasing`). Tool options are laid out like Windows' pen flyout
   (`PenToolbar.Options`: padded, strong headers); command buttons are 44 wide (dense, pen-operated: deliberate).
   Preset buttons are named after their color ("Stift – Blau"). **Stage 2 done 2026-11-17:** the engine is in InkPages (`InkWorkspace`,
   `InkPage`); .xopp/PDF/explorer/tasks/sync stay in the app (sandbox: full test and cross-page drag pass).
   **Demo app `InkPages.Demo` (2026-11-17):** graph pages with in-memory content (`DemoPage` implements `InkPage`),
   the whole toolbar, its own small commands; the development and test app of the library (see Testing). Toolbar
   design reviewed in every state with screenshots (and Codex). The ruler "not shown" in the demo (2026-11-17) was the
   demo targeting page 3 before the first layout (fixed); the app was never affected. Stage 3: README with the
   embedding guide done 2026-11-18 (in the InkPages repository); API docs beyond the XML comments not yet.

## Decided with the user (2026-11-16 morning)

- After paste/duplicate the Auswählen tool stays active (no automatic switch back to the pen).
- Scaling: corner handles only, proportional, stroke width unchanged.
- Tasks: only `<project folder>/todo.txt`; no reminders, no date words in quick add.
- Default name `%F-Notiz-%H-%M`, a PDF's document suggests the PDF's name in its folder.
- Barrel button = always the lasso. Recovery copy 5 s after a change and on focus loss.
- "Meine behalten" after a sync changed the open document: stays unsaved until Ctrl+S.
- `examples/Schaltungen.xopp` reset. Reviews (12 rounds) converged on edge cases; `overnight` merged into main 2026-11-16 (f37018e).
- PDF export approved and done 2026-11-16. Later candidates: page reordering, a text tool.
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
  hover was seen is still put back as a fallback, `InkWorkspace.OnStrokesErased`). WPF had a point eraser natively; WinRT/WinUI ink
  never had one (issue #1530).
- **Questions for the user (review findings kept deliberately, 2026-11-16):**
  - A picture background that fails while being drawn into the PDF is counted as "ohne Hintergrund" instead of
    failing the export (deliberate: one damaged picture should not stop the export).
  - A "clone" picture background remembers its source page by reference; a future page reordering must keep it.
- **Partial eraser approved (user, 2026-11-16)** as a deliberate exception (Windows ink erases whole strokes only):
  **Mode toggle again (user, 2026-11-17 evening):** "Man soll den Radiermodus umschalten können, ohne das
  Zeichenwerkzeug auf Radierer zu ändern, weil man ja eh den Radierer-Stift hat" – one eraser tool (native) and the
  toggle (`PenToolbar.WholeStrokeErase`, an `InkToolbarCustomToggleButton`: it never changes the
  active tool; **inverted 2026-11-18 by the user:** "Ganze Striche radieren", icon ED62 – an eraser in a selection frame –,
  off by default, so the eraser erases exactly unless it is checked; setting `WholeStrokeErasing`); the eraser tool and the pen's eraser button/end follow it. The two-tools variant of the same day
  (mode = the eraser chosen last) is gone. InkToolbar puts toggles after the ruler, so the toggle cannot stand right
  beside the eraser. The user's pen (Lenovo Integrated Pen of the X13 Yoga Gen 4, garaged, 4X81M52316) has two side
  buttons (retailer specs; Lenovo's guide for its Integrated Smart Pen: upper = right-click, lower = erase; not checked
  on the device): the barrel (right-click) button = lasso, the eraser button = erasing with any tool. Partial: the radius is exact and shown as a circle at the pen (hover and
  while erasing) – exactly what is inside the circle disappears, nothing else (cut where the ink's edge, i.e. the
  centerline plus half the stroke width, meets the circle). **Done 2026-11-16.** The eraser tool erases partially
  through the app (presenters in mode None); the eraser end/button with the pen tool active is taken on its inverted
  hover (see above). User: "wichtig ist dass er robust funktioniert". Radius: the toggle's context flyout (right-click, with
  the pen a tap with the barrel button held; 1–20 pt, default 4, previewed as a circle).
- **Pages stay (user, 2026-11-16 evening):** one InkCanvas per page as the file has them; a continuous canvas with
  pages only on export was considered and dropped (upstream .xopp keeps every element on one page, page sizes and
  backgrounds differ, a document-tall InkCanvas runs into surface size limits, every stroke across a boundary would
  need a hidden decision). Instead a selection moves across pages. Each page's ink is drawn only within its page
  (WinUI sizes the InkCanvas' `ContentExternalOutputLink` PlacementVisual to the control – ink moved beyond it by
  `PointTransform` is cut off; z-order does not help), so as soon as the pen of a moved selection is over another page
  the content goes there (`SelectionTool.HandOverIfOverOtherPage` → `EditCommands.TransferToPage`: the file's own
  elements added to the target, all or nothing, then taken from their page) and the drag goes on from there
  (`DragChangedPage` → `InkTools.FollowDrag`). The running pen's events from another page's presenter are converted to
  the drag's page (`InkTools.LocalPosition`); near the view's edge the pages scroll (`DragAutoScroll`, per frame on
  `CompositionTarget.Rendering`: it goes on while the pen holds still). The whole drag – hand-overs and the final move – is one undo step (`CombinedEdit`;
  `IPageEdit.Touches` lets a deleted page drop it from the history). Undo is one history in time order for the document.
- **PDF export of annotations (2026-11-18):** signatures and texts added in Apple's Preview (iPad) are PDF annotations
  (`/Stamp` with an appearance stream, `/FreeText`), not page content: Windows.Data.Pdf shows them, but PDFsharp's
  `XPdfForm` imports only a page's content, so the export lost them. `Services/PdfAnnotationFlattener` draws each
  annotation the screen shows (not Hidden/NoView) with its normal appearance into the background PDF's pages (ISO 32000-1
  12.5.5) before export; PDFsharp's own `PdfAcroForm.Flatten` covers form fields only. A PDF PDFsharp cannot open for
  changing (permissions password) is exported without them and the InfoBar says so (`AnnotationsLeftOut`).
- **iPad import (user, 2026-11-18): Datei → PDF mit iPad-Anmerkungen importieren** – explicit, never automatic: pick
  the annotated PDF, then the .xopp to write; its PDF (the source without the markup) goes beside it with the same name,
  the source stays untouched (`Views/PdfImportCommand`, `Services/PdfMarkupImport`). What iPadOS writes (the user's
  ue1–3, Physik script, GET cover sheet; undocumented, decoded with the help of libfreeform's `pkdrawing.rs` and
  notability-goodnotes' `pencilkit.py`):
  - Handwriting = a `/Stamp` whose `/AAPL:AKExtras` holds a base64 NSKeyedArchiver plist (`AKInkAnnotation2`, read
    with plist-cil: `Model/AppleMarkup`) with a PencilKit drawing (`Model/PencilKitDrawing`, protobuf read with
    Google.Protobuf): `wrdð`+version, or older gzip; drawing coordinates = the page, top left, in points (`drawingSize`).
    The appearance is only a raster image. Strokes may carry a transform (moved); erased strokes keep their whole path
    and list visible pieces (#11) with parametric ranges and **mask polygons = the visible area** (a piece whose mask holds
    none of its points is not shown – checked against Apple's images). Points are curve control points ~1 pt apart, used
    as the polyline; force/opacity were constant (1) in all drawings: no pressure is taken over.
  - Widths measured as ink area ÷ stroke length on Apple's appearance images (eight drawings, all 1.0 pt): pen width
    2.5 → 1.0 pt (factor 0.4), marker ≈ 1.2 × (→ Xournal highlighter, alpha 0x7f).
  - Pasted screenshots are `AKImageAnnotation`s without the original image (`imageAsData` is $null): taken as pictures
    rendered by Windows.Data.Pdf from a temporary PDF holding only those annotations (transparent background,
    `PdfRenderer` `transparent:`, 2 px/pt). Any other annotation that shows something becomes such a picture too
    (stamps, signatures, a drawing that cannot be read, iOS 26 PaperKit drawings: `/PPK` "crdt" data, a further
    undocumented format, not decoded). Links, form fields and annotations without appearance stay in the PDF.
  - iPadOS (PaperKit) leaves an earlier appearance of a drawing as a separate `/Stamp` with only `/PPKID`/`/PPKHash`
    at the drawing's exact rectangle – pixel-identical to the drawing's: dropped with the drawing.
  - `/FreeText` → `<text font="Sans">`, placed and sized as its appearance stream draws it (PDFsharp's `ContentReader`:
    CTM × text matrix at the first show-text operator; Apple drew 11 pt for a /DA of 12 pt), y = baseline − 0.905 em.
  Checked visually against Apple's rendering (side-by-side renders of the imported document's PDF export).
- **Start time (measured 2026-11-18, `App` logs "start:" steps; sandbox script `UiTests/StartupTest.ps1`):** a warm
  start shows the window after ~0.4–0.6 s and the document after ~0.8 s. A cold start takes ~9–12 s on the device:
  Microsoft Defender's real-time protection scans the app's 272 DLLs (self-contained, 237 MB) while they load – MsMpEng
  used 1–1.7 cores during the whole start, the app far less. It happens after new files (every install) and again after
  a while (Defender re-checks, e.g. after signature updates), not because of the app's code; most of it lies before the
  app's code runs (WinUI's own start: the same with the official 2.6.6 runtime as with the fork's). Writing only changed
  files on install did not help (still 11 s), nor did framework-dependent .NET (85 instead of 272 DLLs: cold 9–10 s,
  warm a little slower) – both reverted. Likely the large unsigned native DLLs (the fork's WinUI) cost the scan; not
  measured further. Decided with the user: left as it is (the first start does not matter much); no Defender exclusion.
- **Pages of another PDF (user, 2026-11-18; Seite → Seiten aus PDF einfügen, `InsertPdfPagesCommand`):** upstream
  Xournal++ has one background PDF per document (`LoadHandler::loadBgPdf`, only the first PDF background names the
  file; its own "insert PDF page" features – "With PDF background", "Append New PDF Pages" – use that PDF only). So
  the chosen pages are appended to the **end** of the document's PDF (`PdfPageAppender`; the user agreed the original
  PDF may change; appending keeps the numbers of its pages, another .xopp using the same PDF stays right) and the new
  pages refer to them; annotations of the inserted pages are drawn into them first. A document without PDF takes the
  chosen PDF as its PDF. PDF has no hidden pages: the appended pages show in other viewers (the alternative – an
  attached copy `<name>.xopp.bg.pdf` – was offered to the user, not chosen yet).
- **Pages pane (user, 2026-11-18; side pane tab "Seiten", `Views/PagesPane` + `PageThumbnails`):** thumbnails of all
  pages (tap = go to the page; the page in view is selected), insert a blank page before/after, insert pages of a PDF
  (dialog with the PDF's pages as thumbnails to tap and "vor/nach Seite N" – `InsertPdfPagesCommand`), move up/down
  (also by dragging with finger or mouse; `PagesController.Move`, the file's page order is its `<page>` order as
  upstream), delete (asked). Thumbnails in two layers per page (`PageThumb`, kept per page while the document is
  shown): the background PDF's page rendered once from the loaded PDF, and the page's content (ink, pictures, texts;
  a page without PDF with its paper) drawn by `PdfExport` for that one page without the background PDF into a temporary
  PDF, rendered (transparent) and deleted at once (it holds the user's notes; leftovers of a crash are deleted at start).
  Drawn again only for edited pages (the pages with a view), nearest to the page in view first, only while the pane is
  shown. The first version exported the whole document (with the lecture PDF) per change – slow, the user noticed. The
  pane never replaces its list inside the list's own events (`Show` is enqueued): a crash (0xc000027b in
  Microsoft.UI.Xaml.dll) happened on the device when a page was selected – not reproduced in the sandbox, cause unknown.
  Sandbox: `UiTests/PagesPaneTest.ps1` (thumbnails with ink, taps by mouse/pen/finger, move, delete). Not automated: the
  PDF picker of the insert dialog.
- **Explorer order:** names with numbers by value, as File Explorer shows them (Vorlesung 2 before 11):
  `ProjectTree.NameOrder` = .NET's `CompareOptions.NumericOrdering` + ignore case, ties in ordinal order.
- The page in view is outlined (accent) in documents with more than one page (InkPages `MarkCurrentPage`, set by
  `PagesController` from the document's page count).
- **PDF export:** PDFsharp pinned to 6.2.4, the last MIT version (6.5.0 is AGPL-3.0 with license acceptance – a
  license change for this project is the user's decision).
