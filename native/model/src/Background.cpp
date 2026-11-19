#include "xournal/model/Background.h"

#include "xournal/model/XoppFormat.h"

#include <algorithm>
#include <cwctype>

namespace xournal::Background
{
    bool IsPdf(XmlElement const& background) { return background.AttributeOr(L"type", L"") == L"pdf"; }

    bool IsPixmap(XmlElement const& background) { return background.AttributeOr(L"type", L"") == L"pixmap"; }

    int PdfPageIndex(XmlElement const& background)
    {
        std::wstring pageNo = background.AttributeOr(L"pageno", L"1");
        size_t digits = 0;
        while (digits < pageNo.size() && std::iswdigit(pageNo[digits]))
        {
            ++digits;
        }
        auto n = XoppFormat::ParseInt(std::wstring_view(pageNo).substr(0, digits));
        return std::max(0, n.value_or(1) - 1);
    }

    Color ColorOf(XmlElement const& background) { return ParseColor(background.Attribute(L"color"), Colors::White); }

    BackgroundStyle StyleOf(XmlElement const& background)
    {
        auto style = background.AttributeOr(L"style", L"");
        if (style == L"lined") return BackgroundStyle::Lined;
        if (style == L"ruled") return BackgroundStyle::Ruled;
        if (style == L"graph") return BackgroundStyle::Graph;
        if (style == L"dotted") return BackgroundStyle::Dotted;
        if (style == L"staves") return BackgroundStyle::Staves;
        if (style == L"isodotted") return BackgroundStyle::IsoDotted;
        if (style == L"isograph") return BackgroundStyle::IsoGraph;
        return BackgroundStyle::Plain;
    }

    XmlElementPtr Solid(std::wstring style)
    {
        return XmlElement::Make(L"background", { { L"type", L"solid" }, { L"color", L"#ffffffff" }, { L"style", style } });
    }

    XmlElementPtr Pdf(int pdfPageIndex)
    {
        return XmlElement::Make(L"background", { { L"type", L"pdf" }, { L"pageno", std::to_wstring(pdfPageIndex + 1) } });
    }
}
