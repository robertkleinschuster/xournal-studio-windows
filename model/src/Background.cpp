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

    Color ColorOf(XmlElement const& background)
    {
        // Old Xournal's background color names come first (upstream XmlParserHelper::parseBgColor): "blue" or "green"
        // mean pale paper colors here, not the pen colors of the same name.
        struct NamedPaper
        {
            std::wstring_view name;
            Color color;
        };
        constexpr NamedPaper Papers[]{
            { L"blue", { 0xFF, 0xa0, 0xe8, 0xff } },
            { L"pink", { 0xFF, 0xff, 0xc0, 0xd4 } },
            { L"green", { 0xFF, 0x80, 0xff, 0xc0 } },
            { L"orange", { 0xFF, 0xff, 0xc0, 0x80 } },
            { L"yellow", { 0xFF, 0xff, 0xff, 0x80 } },
        };
        auto name = background.Attribute(L"color");
        for (auto const& paper : Papers)
        {
            if (name && *name == paper.name)
            {
                return paper.color;
            }
        }
        return ParseColor(name, Colors::White);
    }

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
