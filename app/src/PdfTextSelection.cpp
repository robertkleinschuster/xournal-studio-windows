#include "PdfTextSelection.h"

#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"
#include "xournal/pdf/PdfPlaces.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace xournal::app
{
    namespace
    {
        // How near a character the mouse must be (page units): pressing on text, then dragging (beside a line's end or
        // between lines the selection keeps its last character).
        constexpr double PressTolerance = 2;
        constexpr double DragTolerance = 6;

        class ClipboardScope
        {
        public:
            explicit ClipboardScope(HWND owner)
            {
                if (!OpenClipboard(owner)) ThrowLastError("OpenClipboard (PDF text)");
            }
            ~ClipboardScope() { CloseClipboard(); }
            ClipboardScope(ClipboardScope const&) = delete;
            ClipboardScope& operator=(ClipboardScope const&) = delete;
        };

        pdftext::CharRange Union(pdftext::CharRange a, pdftext::CharRange b)
        {
            int first = std::min(a.first, b.first);
            return { first, std::max(a.first + a.count, b.first + b.count) - first };
        }
    }

    PdfTextSelection::PdfTextSelection(HWND owner, DocumentView& view) : owner(owner), view(view)
    {
    }

    PdfTextSelection::~PdfTextSelection()
    {
        *alive = false;
    }

    void PdfTextSelection::DocumentShown()
    {
        Clear();
        ++generation;
        text.reset();
        openPage.reset();
        openPdfPage = -1;
    }

    void PdfTextSelection::PdfLoaded()
    {
        DocumentShown(); // the PDF may have changed (pages appended): its text is read anew
        auto doc = view.Document();
        if (!doc || !doc->PdfPath()) return;
        auto file = *doc->PdfPath();
        UiQueue::RunInBackground("pdf text selection: read the PDF's text", [this, file, alive = alive, wanted = generation] {
            if (!pdftext::Available())
            {
                LogError(std::format("pdf text selection: the PDF's text cannot be selected ({})", pdftext::UnavailableReason()));
                return;
            }
            std::shared_ptr<pdftext::TextDocument const> loaded;
            try
            {
                loaded = std::make_shared<pdftext::TextDocument const>(io::Files::ReadAll(file));
            }
            catch (...)
            {
                LogException(std::format("pdf text selection: reading the text of {}", ToUtf8(file.native())));
                return;
            }
            UiQueue::Post("pdf text selection: the PDF's text", [this, alive, wanted, loaded] {
                if (!*alive || wanted != generation) return; // another document or PDF since
                text = loaded;
                Log(std::format("pdf text selection: the PDF's text is ready ({} pages)", loaded->PageCount()));
            });
        });
    }

    pdftext::TextPage* PdfTextSelection::PageAt(int page, inkpages::PointF point, Point& shown)
    {
        auto doc = view.Document();
        auto background = view.Pdf();
        if (!text || !doc || !background || page < 0 || page >= static_cast<int>(doc->Pages().size())) return nullptr;
        auto const& content = *doc->Pages()[page];
        auto at = pdf::ToShown(content, { point.x, point.y }, [&](int pdfPage) { return background->ShownSize(pdfPage); });
        int pdfPage = content.PdfPageIndex();
        if (!at || pdfPage < 0 || pdfPage >= text->PageCount()) return nullptr;
        if (pdfPage != openPdfPage)
        {
            openPage.reset();
            openPdfPage = -1;
            openPage = text->OpenPage(pdfPage);
            openPdfPage = pdfPage;
        }
        shown = *at;
        return openPage.get();
    }

    std::optional<int> PdfTextSelection::CharAt(int page, inkpages::PointF point, double tolerance)
    {
        Point shown;
        auto textPage = PageAt(page, point, shown);
        return textPage ? textPage->CharAt(shown, tolerance) : std::nullopt;
    }

    bool PdfTextSelection::TextAt(int page, inkpages::PointF point)
    {
        try
        {
            return CharAt(page, point, PressTolerance).has_value();
        }
        catch (...)
        {
            LogException(std::format("pdf text selection: is there text on page {}", page + 1));
            return false; // the press stays InkPages' (a lasso)
        }
    }

    void PdfTextSelection::Gesture(inkpages::MouseGesture const& gesture)
    {
        try
        {
            switch (gesture.phase)
            {
            case inkpages::MousePhase::Begin:
            {
                Clear();
                auto at = CharAt(gesture.page, gesture.point, PressTolerance);
                if (!at) return;
                selecting = Selecting{ gesture.page, *at, gesture.clicks };
                Extend(*at);
                break;
            }
            case inkpages::MousePhase::Move:
            case inkpages::MousePhase::End:
                if (!selecting) return;
                if (auto at = CharAt(selecting->page, gesture.point, DragTolerance)) Extend(*at);
                if (gesture.phase == inkpages::MousePhase::End)
                {
                    selecting.reset();
                    selectedText = openPage->Text(range);
                    Log(std::format("pdf text selection: {} characters on page {}", selectedText.size(), selectedPage + 1));
                    if (changed) changed();
                }
                break;
            case inkpages::MousePhase::Cancel:
                Clear();
                break;
            }
        }
        catch (...)
        {
            LogException("pdf text selection: the mouse's gesture");
            Clear();
        }
    }

    void PdfTextSelection::Extend(int to)
    {
        // By characters, words or lines (a double or triple click) from the anchor's unit to the mouse's.
        auto unit = [&](int index) -> pdftext::CharRange {
            switch (selecting->clicks)
            {
            case 1: return { index, 1 };
            case 2: return openPage->WordAt(index);
            default: return openPage->LineAt(index);
            }
        };
        auto next = Union(unit(selecting->anchor), unit(to));
        if (selectedPage == selecting->page && next == range) return;
        range = next;
        selectedPage = selecting->page;
        auto doc = view.Document();
        auto background = view.Pdf();
        if (!doc || !background) return;
        auto const& content = *doc->Pages()[selectedPage];
        std::vector<Rect> rects;
        for (auto const& r : openPage->Rects(range))
        {
            rects.push_back(pdf::FromShown(content, r, [&](int pdfPage) { return background->ShownSize(pdfPage); }));
        }
        view.MarkTextSelection(selectedPage, std::move(rects));
    }

    void PdfTextSelection::Copy()
    {
        if (selectedText.empty()) return;
        ClipboardScope open(owner);
        if (!EmptyClipboard()) ThrowLastError("EmptyClipboard (PDF text)");
        size_t bytes = (selectedText.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!memory) ThrowLastError("GlobalAlloc (PDF text)");
        void* p = GlobalLock(memory);
        if (!p)
        {
            GlobalFree(memory);
            ThrowLastError("GlobalLock (PDF text)");
        }
        std::memcpy(p, selectedText.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory))
        {
            GlobalFree(memory);
            ThrowLastError("SetClipboardData(CF_UNICODETEXT)");
        }
        Log(std::format("pdf text selection: {} characters copied", selectedText.size()));
    }

    void PdfTextSelection::Clear()
    {
        selecting.reset();
        bool had = selectedPage >= 0;
        selectedPage = -1;
        range = {};
        selectedText.clear();
        if (!had) return;
        view.MarkTextSelection(-1, {});
        if (changed) changed();
    }
}
