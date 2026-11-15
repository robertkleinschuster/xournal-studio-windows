"""Builds public example documents: the user's handwriting from graph pages of two exercises,
with self-written exercise texts (PDF background + copied areas) instead of the university's.
Usage: python make_examples.py <source dir> <output dir>"""
import base64, copy, gzip, io, os, subprocess, sys
import xml.etree.ElementTree as ET
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfgen import canvas
from reportlab.platypus import Paragraph
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.fonts import addMapping

FONTS = "C:/msys64/ucrt64/share/fonts/TTF/"
pdfmetrics.registerFont(TTFont("Serif", FONTS + "DejaVuSerif.ttf"))
pdfmetrics.registerFont(TTFont("Serif-Bold", FONTS + "DejaVuSerif-Bold.ttf"))
for italic in (0, 1):
    addMapping("Serif", 0, italic, "Serif")
    addMapping("Serif", 1, italic, "Serif-Bold")

SRC, OUT = sys.argv[1], sys.argv[2]
PDFTOPPM = r"C:\msys64\ucrt64\bin\pdftoppm.exe"
W, H = A4
STROKE_ATTRS = {"tool", "color", "width", "capStyle", "style", "fill", "ts", "fn"}

CIRCUIT_TEXTS = [
    "<b>Beispiel 1 (Überlagerungsprinzip).</b> Die Schaltung enthält eine Spannungsquelle U<sub>q1</sub> und eine "
    "Stromquelle I<sub>q3</sub>. Bestimmen Sie den Strom durch R<sub>3</sub>, indem Sie die Beiträge der beiden "
    "Quellen getrennt berechnen und addieren. Werte: R<sub>1</sub> = 120 Ω, R<sub>2</sub> = 220 Ω, "
    "R<sub>3</sub> = 330 Ω, R<sub>4</sub> = 100 Ω, U<sub>q1</sub> = 12 V, I<sub>q3</sub> = 1 A.",
    "(a) Ersetzen Sie die Stromquelle durch einen Leerlauf und berechnen Sie den Teilstrom I'<sub>R3</sub>. "
    "(b) Ersetzen Sie die Spannungsquelle durch einen Kurzschluss und berechnen Sie I''<sub>R3</sub>.",
    "(c) Berechnen Sie die Spannung U<sub>R3</sub>, wenn nur die Stromquelle aktiv ist.",
    "(d) Überprüfen Sie das Ergebnis mit einer Simulation.",
    "<b>Beispiel 2 (Ersatzquelle).</b> Bestimmen Sie U<sub>0</sub> und R<sub>i</sub> der Ersatzquelle an A–B.",
    "(a) Berechnen Sie die Leerlaufspannung zwischen A und B. (b) Berechnen Sie den Innenwiderstand bei "
    "abgeschalteten Quellen. (c) Bei welchem Lastwiderstand wird die abgegebene Leistung maximal?",
]
MATH_TEXTS = [
    "<b>Beispiel 2.1.</b> Bestimmen Sie, ob die folgenden Grenzwerte existieren, und geben Sie gegebenenfalls "
    "ihren Wert an: (a) lim<sub>x→2</sub> (x² − 4)/(x − 2), (b) lim<sub>x→∞</sub> (√(x² + 3x) − x), "
    "(c) lim<sub>x→0</sub> (√(x + 9) − 3)/x.",
    "<b>Beispiel 2.2.</b> Untersuchen Sie die Folge a<sub>n</sub> = (2n² + 1)/(n² − n + 3) auf Konvergenz. "
    "(a) Zeigen Sie, dass die Folge beschränkt ist. (b) Bestimmen Sie den Grenzwert. "
    "(c) Ab welchem n gilt |a<sub>n</sub> − 2| &lt; 0,01? (d) Ist die Folge monoton?",
    "<b>Beispiel 2.3.</b> Skizzieren Sie den Graphen von f(x) = 1/(x + 2) und bestimmen Sie alle Asymptoten.",
]


def fit_paragraph(text, w, h):
    for size in (11, 10, 9, 8, 7, 6, 5):
        style = ParagraphStyle("p", fontName="Serif", fontSize=size, leading=size * 1.2)
        p = Paragraph(text, style)
        _, ph = p.wrap(w - 4, 10000)
        if ph <= h - 2:
            return p, ph
    return p, ph


def draw_block(c, text, x, top, w, h):
    p, ph = fit_paragraph(text, w, h)
    p.drawOn(c, x + 2, H - top - 1 - ph)


def draw_circuit(c, x, top):
    """A small original circuit sketch: source, three resistors."""
    y = H - top
    c.setLineWidth(1)
    c.line(x, y, x + 200, y); c.line(x, y - 100, x + 200, y - 100)
    c.line(x, y, x, y - 30); c.line(x, y - 70, x, y - 100)
    c.circle(x, y - 50, 20); c.drawCentredString(x - 32, y - 54, "Uq1")
    for i, name in enumerate(["R1", "R2", "R3"]):
        cx = x + 70 + i * 65
        c.line(cx, y, cx, y - 25); c.rect(cx - 8, y - 75, 16, 50); c.line(cx, y - 75, cx, y - 100)
        c.drawString(cx + 12, y - 54, name)


def make_pdf(path, title, texts, sizes, circuit):
    """Page 1: title + all text blocks (one per original copied area, same sizes); returns block rects."""
    c = canvas.Canvas(path, pagesize=A4)
    c.setTitle(title)
    c.setFont("Serif-Bold", 15)
    c.drawString(70, H - 60, title)
    c.setFont("Serif", 9)
    c.drawString(70, H - 78, "Beispieldatei für Xournal Studio for Windows – Aufgabentexte frei erfunden.")
    rects, top = [], 100
    for text, (w, h) in zip(texts, sizes):
        draw_block(c, text, 70, top, w, h)
        rects.append((70, top, w, h))
        top += h + 14
    if circuit:
        c.setFont("Serif", 9)
        draw_circuit(c, 120, top + 30)
    c.showPage()
    c.save()
    return rects


def area_pdf(text, w, h):
    buf = io.BytesIO()
    c = canvas.Canvas(buf, pagesize=(w, h))
    p, ph = fit_paragraph(text, w, h)
    p.drawOn(c, 2, h - 1 - ph)
    c.showPage(); c.save()
    return buf.getvalue()


def crop_png(pdf, rect, tmp):
    x, top, w, h = rect
    dpi = 240
    s = dpi / 72
    base = os.path.join(tmp, "crop")
    subprocess.run([PDFTOPPM, "-r", str(dpi), "-f", "1", "-l", "1", "-singlefile", "-png",
                    "-x", str(int(x * s)), "-y", str(int(top * s)), "-W", str(int(w * s)), "-H", str(int(h * s)),
                    pdf, base], check=True)
    data = open(base + ".png", "rb").read()
    return data, int(w * s), int(h * s)


def build(source, name, title, texts, circuit, teximage_first):
    root = ET.fromstring(gzip.open(os.path.join(SRC, source)).read())
    graph = [p for p in root.iter("page") if p.find("background").get("type") != "pdf"]
    pictures = [e for p in graph for e in p.iter() if e.tag in ("image", "teximage")]
    sizes = [(float(e.get("right")) - float(e.get("left")), float(e.get("bottom")) - float(e.get("top"))) for e in pictures]
    pdf = os.path.join(OUT, name + ".pdf")
    rects = make_pdf(pdf, title, texts, sizes, circuit)

    out = ET.Element("xournal", {"creator": "xournalpp 1.2.3", "fileversion": "4"})
    ET.SubElement(out, "title").text = "Xournal++ document - see https://xournalpp.github.io/"
    first = ET.SubElement(out, "page", {"width": "595.27559", "height": "841.88976"})
    ET.SubElement(first, "background", {"type": "pdf", "domain": "absolute", "filename": name + ".pdf", "pageno": "1"})
    layer = ET.SubElement(first, "layer")
    # A highlighter mark under the first heading (made up, not the user's).
    hl = ET.SubElement(layer, "stroke", {"tool": "highlighter", "color": "#ffff007f", "width": "8.5"})
    hl.text = "70 %.2f 230 %.2f" % (rects[0][1] + 7, rects[0][1] + 7)

    index = 0
    for page in graph:
        new = ET.SubElement(out, "page", {"width": page.get("width"), "height": page.get("height")})
        ET.SubElement(new, "background", {"type": "solid", "color": "#ffffffff", "style": "graph"})
        for old_layer in page.iter("layer"):
            new_layer = ET.SubElement(new, "layer")
            for e in old_layer:
                if e.tag == "stroke":
                    s = ET.SubElement(new_layer, "stroke", {k: v for k, v in e.attrib.items() if k in STROKE_ATTRS})
                    s.text = e.text
                elif e.tag in ("image", "teximage"):
                    bounds = {k: e.get(k) for k in ("left", "top", "right", "bottom")}
                    w, h = sizes[index]
                    if teximage_first and index == 0:
                        t = ET.SubElement(new_layer, "teximage", {"text": "", **bounds})
                        t.text = base64.b64encode(area_pdf(texts[index], w, h)).decode()
                    else:
                        png, pw, ph = crop_png(pdf, rects[index], OUT)
                        i = ET.SubElement(new_layer, "image", {**bounds, "natural_size": f"{pw} {ph}"})
                        i.text = base64.b64encode(png).decode()
                    index += 1
    os.remove(os.path.join(OUT, "crop.png")) if os.path.exists(os.path.join(OUT, "crop.png")) else None
    data = b'<?xml version="1.0" standalone="no"?>\n' + ET.tostring(out, encoding="utf-8")
    with gzip.open(os.path.join(OUT, name + ".xopp"), "wb") as f:
        f.write(data)
    print(name, "graph pages:", len(graph), "pictures:", len(pictures))


os.makedirs(OUT, exist_ok=True)
build("GET_UE_4_ETBME6.xopp", "Schaltungen", "Übung Elektrotechnik – Gleichstromnetzwerke", CIRCUIT_TEXTS, True, True)
build("ue6.xopp", "Grenzwerte", "Übung Analysis – Grenzwerte und Folgen", MATH_TEXTS, False, False)
