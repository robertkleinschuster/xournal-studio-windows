# Xournal Studio for Windows – native (C++/Win32)

The C# WinUI app rebuilt as a native C++20 app (plan: `docs/CPP-UMZUG.md`). Windows APIs first; third-party code only
where Windows has nothing (zlib for gzip), fetched at a pinned release and linked statically.

```powershell
.\build.ps1            # configure (Visual Studio's CMake), build Release, run the unit tests (CTest + GoogleTest)
.\build.ps1 -NoTests
```

## Modules

Each module is a static library with its own folder, public headers in `<module>/include/xournal/<module>/` and
internals in `<module>/src/`. The CMake link dependencies are the only allowed direction:

```
base  <-  model  <-  io  <-  (later: services: pdf, git, clipboard, recovery, tasks)  <-  app (UI)
```

| Module | Target | Job | Uses |
|---|---|---|---|
| `base` | `XournalBase` | `Error` (HRESULT + system text, logged when thrown), `Log`, UTF-8/UTF-16 and case-insensitive text, `Paths` (full/relative paths as Windows treats them) | Windows |
| `model` | `XournalModel` | The document in memory: XML tree (`Xml`), `XoppDocument`, `XoppPage` with `PageInk` (stroke elements bound to the ink layer) and `PageImages`, `Strokes` (element <-> ink, `StrokeShape`), `XoppImage`, `XoppText`, `XoppClip`, `Background`, `BackgroundRuling`, references (`src/References.h`), `Snapshot`, `Attachments`, `CopyPages`, `DocumentNames`, `TodoTxt`. No file contents read or written, no UI. | base; crypt32 (base64), Windows' ICU (characters) |
| `io` | `XournalIo` | `XmlText` (XmlLite), `Gzip` (zlib), `Files` (whole-file reads, `<file>~` then replace), `DocumentFile` (load/save/Save As with attachments) | model; xmllite, shlwapi, zlib |
| `tests` | `XournalTests` | Unit tests of all modules, ported from `XournalStudio.Tests` with the same sample XML, plus a lossless round trip of `examples\*.xopp` | io, GoogleTest |

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
