#include "xournal/pdf/ClipPicture.h"

#include "xournal/base/Error.h"
#include "xournal/model/XoppClip.h"
#include "xournal/model/XoppFormat.h"
#include "xournal/pdf/DocumentExport.h"

#include <algorithm>
#include <format>

namespace xournal::pdf
{
    RenderedArea RenderClip(XmlElement const& clip, double pixelsPerPoint, int maxSide)
    {
        Size size = XoppClip::GetSize(clip);
        // A clip of one straight line is as thin as the line: at least a point on each side.
        double width = std::max(1.0, size.width), height = std::max(1.0, size.height);
        auto page = XmlElement::Make(L"page", {
            { L"width", XoppFormat::FormatNumber(width) },
            { L"height", XoppFormat::FormatNumber(height) },
        });
        // Transparent plain paper: only the clip's elements are drawn.
        page->Append(XmlElement::Make(L"background", { { L"type", L"solid" }, { L"color", L"#ffffff00" }, { L"style", L"plain" } }));
        auto layer = XmlElement::Make(L"layer");
        for (auto const& e : clip.Elements()) layer->Append(e->Clone());
        page->Append(layer);
        auto root = XmlElement::Make(L"xournal");
        root->Append(page);

        DocumentExportSource source{ root, std::nullopt, { std::nullopt } };
        ExportResult result;
        auto pages = PagesOf(source, result);
        if (pages.size() != 1) ThrowInvalidData(std::format("clip picture: {} pages exported, expected 1", pages.size()));
        auto bytes = ExportToMemory(pages, result);
        double scale = std::min(pixelsPerPoint, maxSide / std::max(width, height));
        return PdfAreaRenderer(bytes).Render(0, { 0, 0, width, height }, scale);
    }
}
