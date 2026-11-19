#pragma once

// PDF export: pages described in the model's terms (StrokeShape, XoppText, picture placement, background style) written
// as PDF with qpdf and Windows' own components.
//
// - A PDF background page is kept exactly: its annotations' appearances are flattened into it first (qpdf; Hidden and
//   NoView left out, as a viewer shows it), then its content becomes a form XObject copied unchanged (text stays text,
//   images keep their data and filters), stretched to the page.
// - Solid backgrounds: paper color and BackgroundRuling's lines and dots; picture backgrounds stretched to the page.
// - Strokes: StrokeShape::Parts as PDF paths (one per segment of a pressure stroke, with its width), caps, dashes,
//   fillings; alpha (highlighters) through ExtGState.
// - Pictures: decoded by WIC, written as Flate samples with a Flate soft mask for alpha (never re-encoded as JPEG);
//   JPEG data passed through unchanged; a one-page PDF picture (copied area) as a form.
// - Texts: laid out by DirectWrite; TrueType fonts embedded as subsets (Font Subsetting API) with a ToUnicode map;
//   fonts that cannot be embedded that way (CFF outlines, restricted licenses, simulated bold/oblique) as outlines.
// - /Info holds only /Producer; the file ID is deterministic.

#include "xournal/model/Background.h"
#include "xournal/model/Color.h"
#include "xournal/model/StrokeShape.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppText.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace xournal::pdf
{
    /// A page of a PDF file, stretched to the page.
    struct PdfPageBackground
    {
        std::filesystem::path file;
        int pageIndex = 0; // 0-based; beyond the last page: the last page (as the C# export)
    };

    /// Paper color and ruling (see BackgroundRuling).
    struct SolidBackground
    {
        Color paper = Colors::White;
        BackgroundStyle style = BackgroundStyle::Plain;
        std::optional<std::wstring> config;
    };

    /// A picture file (anything WIC decodes), stretched to the page.
    struct PictureBackground
    {
        std::filesystem::path file;
    };

    /// No background (white), or a background whose file is unknown: counted as missing.
    struct MissingBackground {};

    using PageBackground = std::variant<SolidBackground, PdfPageBackground, PictureBackground, MissingBackground>;

    /// A picture's data (PNG, JPEG, ... or a one-page PDF) stretched over placement.size, then placed by its transform.
    struct Picture
    {
        std::vector<std::uint8_t> data;
        XoppImage::Placement placement;
    };

    using PageElement = std::variant<StrokeShape, Picture, XoppText>;

    struct ExportPage
    {
        double width = 0, height = 0; // points
        PageBackground background;
        std::vector<PageElement> elements; // bottom to top
    };

    struct ExportOptions
    {
        std::string producer = "Xournal Studio for Windows"; // nothing else goes into /Info
    };

    /// What the export left out (each case is also logged).
    struct ExportResult
    {
        int pages = 0;
        int missingBackgrounds = 0;      // PDF or picture not found/readable: the page stays white under its notes
        bool annotationsLeftOut = false; // a background PDF's annotations could not be flattened: exported without them
        int textRunsAsOutlines = 0;      // glyph runs drawn as outlines (font not embeddable as a TrueType subset)
        int skippedElements = 0;         // elements the app does not draw (set by DocumentExport)
    };

    /// Writes the pages to `target` ("<target>~" first, then replacing it: io::Files). Never over a background PDF of
    /// the pages (throws). Blocking; call it off the UI thread. Throws xournal::Error.
    ExportResult Export(std::span<ExportPage const> pages, std::filesystem::path const& target, ExportOptions const& options = {});

    /// The same into memory (e.g. thumbnails: nothing of the user's notes goes to a file).
    std::vector<std::uint8_t> ExportToMemory(std::span<ExportPage const> pages, ExportResult& result, ExportOptions const& options = {});
}
