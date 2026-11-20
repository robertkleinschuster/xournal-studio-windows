#pragma once

// The mouse selects the background PDF's text, as in a PDF viewer: a drag that starts on the PDF's text (with no ink,
// picture or text of the file there - InkPages asks `mouseClaims` only then) selects the characters from the press to
// the mouse in reading order; a double click selects a word, a triple click a line (dragging on extends by words or
// lines). The selection is marked in the system's selection color (DocumentView::MarkTextSelection, as the search hit)
// and copied as plain text (Strg+C, Bearbeiten -> Kopieren). It is read-only and lies on one page. PDFium does the work
// (pdftext::TextPage: the character at a point, the range's rectangles and text).
//
// The PDF's text is read on the thread pool once the background PDF is loaded; until then (and without PDFium) no
// drag is claimed. UI thread.

#include "DocumentView.h"

#include "inkpages/WritingSurface.h"
#include "xournal/pdftext/PdfText.h"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace xournal::app
{
    class PdfTextSelection
    {
    public:
        PdfTextSelection(HWND owner, DocumentView& view);
        ~PdfTextSelection();
        PdfTextSelection(PdfTextSelection const&) = delete;
        PdfTextSelection& operator=(PdfTextSelection const&) = delete;

        /// Another document is shown: the selection and the old PDF's text go.
        void DocumentShown();
        /// The shown document's background PDF is (again) loaded: its text is read in the background.
        void PdfLoaded();

        /// Whether the PDF's text lies at a point of a document page (page units): InkPages' `mouseClaims`.
        bool TextAt(int page, inkpages::PointF point);
        /// A claimed mouse gesture (InkPages' `mouseGesture`).
        void Gesture(inkpages::MouseGesture const& gesture);

        bool HasSelection() const { return !selectedText.empty(); }
        /// Puts the selected text on the clipboard (CF_UNICODETEXT). Throws xournal::Error when the clipboard fails.
        void Copy();
        void Clear();

        /// The selection changed (the menu's commands follow).
        std::function<void()> changed;

    private:
        struct Selecting
        {
            int page = 0;   // document page
            int anchor = 0; // the character pressed on
            int clicks = 1; // 1: characters, 2: words, 3: lines
        };

        /// The PDF's text page shown on document page `page` (kept open while the mouse stays on it), and the point in
        /// its shown coordinates.
        pdftext::TextPage* PageAt(int page, inkpages::PointF point, Point& shown);
        std::optional<int> CharAt(int page, inkpages::PointF point, double tolerance);
        void Extend(int to);

        HWND owner;
        DocumentView& view;
        std::shared_ptr<bool> alive = std::make_shared<bool>(true);
        std::uint64_t generation = 0;                       // the shown PDF's (texts read for an older one are dropped)
        std::shared_ptr<pdftext::TextDocument const> text;  // the shown document's PDF text (nothing: none yet)
        std::unique_ptr<pdftext::TextPage> openPage;
        int openPdfPage = -1;
        std::optional<Selecting> selecting;                 // while a gesture runs
        int selectedPage = -1;
        pdftext::CharRange range;
        std::wstring selectedText;
    };
}
