# Xournal Studio for Windows

A small note app for Windows 11 pen convertibles that reads and writes [Xournal++](https://xournalpp.github.io/)
`.xopp` files, built with native WinUI 3 controls (Windows Ink, InkToolbar, ScrollView).
It covers one workflow well: annotating lecture PDFs and solving exercises on graph paper with the pen.

## Features

- Pen, highlighter and eraser with Windows ink (pressure, low latency, the pen's eraser end); touch scrolls
  and zooms, never draws.
- Shape recognition by Windows ink analysis: hand-drawn circles, ellipses, triangles, rectangles and polygons
  become clean shapes.
- Native `InkToolbar` custom tools for lasso/rectangle selection, moving and scaling content, shape drawing, and
  copying PDF areas.
- Images: pictures in documents are shown (also areas copied by other Xournal++ builds); a bitmap from the
  clipboard – e.g. a part of an exercise cut out with the Snipping Tool (Win+Shift+S) – is pasted below the
  content of the current page as a normal Xournal++ image.
- Pages: PDF background, graph paper for new pages, insert/delete.
- Undo/redo; saves automatically when the window loses focus or closes.
- Files stay in the upstream Xournal++ format: nothing is added that Xournal++ does not know, and nothing
  the file contains is lost.

## Build and install

Requires Windows 11 and the .NET 10 SDK. The pen workspace is the [InkPages](https://github.com/robertkleinschuster/InkPages)
library, a git submodule: clone with `--recurse-submodules` (or run `git submodule update --init`).

```powershell
dotnet test XournalStudio.Tests   # unit tests
.\install.ps1                     # tests, publish, install to %LOCALAPPDATA%\Programs\Xournal Studio for Windows
```

`examples/` contains two exercise notebooks to try it with. Development notes: [CLAUDE.md](CLAUDE.md).
