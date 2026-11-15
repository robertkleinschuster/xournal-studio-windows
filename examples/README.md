# Example documents

Two exercise notebooks in the typical workflow: an exercise PDF as background, then graph pages
with parts of the exercise pasted at the top and the handwritten solution below.

| File | Content |
|---|---|
| `Schaltungen.xopp` + `.pdf` | DC circuits: circuit diagrams (lines, rectangles, circles, arrows) and calculations; colored highlighter wires. One pasted area is a `<teximage>` with an embedded one-page PDF (how other Xournal++ builds store copied areas), the others are plain `<image>` PNGs. |
| `Grenzwerte.xopp` + `.pdf` | Analysis: limits, polynomial division, long formulas in several colors. |

The handwriting is real (pressure-sensitive pen strokes from a Wacom AES pen). The exercise texts
and PDFs are made up for this repository (`tools/make_examples.py`), so the solutions do not match
them exactly. The files are written in the upstream Xournal++ format (fileversion 4) and open in
Xournal++ as well.
