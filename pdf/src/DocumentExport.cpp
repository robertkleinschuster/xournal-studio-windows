#include "xournal/pdf/DocumentExport.h"

#include "xournal/base/Error.h"
#include "xournal/base/Paths.h"
#include "xournal/model/Snapshot.h"
#include "xournal/model/Strokes.h"
#include "xournal/model/XoppFormat.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppText.h"

namespace xournal::pdf
{
    namespace
    {
        PageBackground BackgroundOf(XmlElement const* bg, DocumentExportSource const& source, size_t pageIndex)
        {
            if (!bg)
            {
                return SolidBackground{}; // upstream reads a page without background as plain white paper
            }
            if (Background::IsPdf(*bg))
            {
                if (source.backgroundPdf) return PdfPageBackground{ *source.backgroundPdf, Background::PdfPageIndex(*bg) };
                return MissingBackground{};
            }
            if (Background::IsPixmap(*bg))
            {
                auto const& file = pageIndex < source.pictureBackgrounds.size() ? source.pictureBackgrounds[pageIndex] : std::nullopt;
                if (file) return PictureBackground{ *file };
                return MissingBackground{};
            }
            SolidBackground solid{ Background::ColorOf(*bg), Background::StyleOf(*bg), std::nullopt };
            if (auto const* config = bg->Attribute(L"config")) solid.config = *config;
            return solid;
        }
    }

    DocumentExportSource PrepareExport(XoppDocument& doc)
    {
        DocumentExportSource source;
        source.snapshot = TakeSnapshot(doc, doc.ReferenceBase());
        source.backgroundPdf = doc.PdfPath();
        for (auto const& page : doc.Pages())
        {
            std::optional<std::filesystem::path> picture = page->BackgroundImagePath();
            if (!picture)
            {
                if (auto clone = page->BackgroundCloneSource()) picture = clone->BackgroundImagePath();
            }
            source.pictureBackgrounds.push_back(std::move(picture));
        }
        return source;
    }

    std::vector<ExportPage> PagesOf(DocumentExportSource const& source, ExportResult& result)
    {
        if (!source.snapshot)
        {
            ThrowInvalidData("pdf export: no snapshot");
        }
        std::vector<ExportPage> pages;
        for (auto const& pageElement : source.snapshot->Elements(L"page"))
        {
            ExportPage page;
            page.width = XoppFormat::ReadNumber(*pageElement, L"width", XoppFormat::A4Width);
            page.height = XoppFormat::ReadNumber(*pageElement, L"height", XoppFormat::A4Height);
            auto bg = pageElement->Element(L"background");
            page.background = BackgroundOf(bg.get(), source, pages.size());
            for (auto const& layer : pageElement->Elements(L"layer"))
            {
                for (auto const& e : layer->Elements())
                {
                    if (e->Name() == L"stroke")
                    {
                        if (auto shape = Strokes::ReadShape(*e)) page.elements.emplace_back(std::move(*shape));
                    }
                    else if (XoppImage::IsImage(*e))
                    {
                        page.elements.emplace_back(Picture{ XoppImage::GetData(*e), XoppImage::GetPlacement(*e) });
                    }
                    else if (XoppText::IsText(*e))
                    {
                        page.elements.emplace_back(XoppText::Read(*e));
                    }
                    else
                    {
                        ++result.skippedElements; // an element upstream may have that this app does not draw
                    }
                }
            }
            pages.push_back(std::move(page));
        }
        return pages;
    }

    ExportResult ExportDocument(DocumentExportSource const& source, std::filesystem::path const& target, ExportOptions const& options)
    {
        // Never over the document's PDF - also when no page shows it (Export checks the pages' backgrounds).
        if (source.backgroundPdf && Paths::Same(*source.backgroundPdf, target))
        {
            ThrowHr("pdf export: the target is the document's background PDF", HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
        }
        ExportResult counts;
        auto pages = PagesOf(source, counts);
        ExportResult result = Export(pages, target, options);
        result.skippedElements = counts.skippedElements;
        return result;
    }
}
