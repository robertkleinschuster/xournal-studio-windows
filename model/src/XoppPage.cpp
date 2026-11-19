#include "xournal/model/XoppPage.h"

#include "xournal/model/Strokes.h"
#include "xournal/model/XoppFormat.h"
#include "xournal/model/XoppText.h"

#include <stdexcept>

namespace xournal
{
    namespace
    {
        XmlElementPtr Prepared(XmlElementPtr element)
        {
            if (!element->Element(L"background"))
            {
                element->Prepend(Background::Solid(L"plain"));
            }
            if (!element->Element(L"layer"))
            {
                element->Append(XmlElement::Make(L"layer"));
            }
            return element;
        }

        double Dimension(XmlElement const& e, std::wstring_view name, double fallback)
        {
            auto text = e.Attribute(name);
            return text ? XoppFormat::ParseDouble(*text).value_or(fallback) : fallback;
        }
    }

    XoppPage::XoppPage(XmlElementPtr pageElement)
        : element(Prepared(std::move(pageElement))),
          width(Dimension(*element, L"width", XoppFormat::A4Width)),
          height(Dimension(*element, L"height", XoppFormat::A4Height)),
          ink(*element),
          images(*element)
    {
    }

    std::shared_ptr<XoppPage> XoppPage::FromXml(XmlElementPtr element)
    {
        return std::make_shared<XoppPage>(std::move(element));
    }

    std::shared_ptr<XoppPage> XoppPage::CreateGraph(double width, double height)
    {
        auto e = XmlElement::Make(L"page", { { L"width", XoppFormat::FormatShortest(width) },
            { L"height", XoppFormat::FormatShortest(height) } });
        e->Append(Background::Solid(L"graph"));
        e->Append(XmlElement::Make(L"layer"));
        return FromXml(e);
    }

    std::shared_ptr<XoppPage> XoppPage::CreatePdf(double width, double height, int pdfPageIndex)
    {
        auto e = XmlElement::Make(L"page", { { L"width", XoppFormat::FormatShortest(width) },
            { L"height", XoppFormat::FormatShortest(height) } });
        e->Append(Background::Pdf(pdfPageIndex));
        e->Append(XmlElement::Make(L"layer"));
        return FromXml(e);
    }

    XmlElement& XoppPage::Background() const
    {
        auto background = element->Element(L"background");
        if (!background)
        {
            throw std::logic_error("XoppPage: a page always has a background"); // added on construction
        }
        return *background;
    }

    std::vector<XmlElementPtr> XoppPage::Texts() const
    {
        std::vector<XmlElementPtr> texts;
        for (auto const& layer : element->Elements(L"layer"))
        {
            for (auto const& e : layer->Elements())
            {
                if (XoppText::IsText(*e))
                {
                    texts.push_back(e);
                }
            }
        }
        return texts;
    }

    std::vector<XmlElementPtr> const& XoppPage::DrawnStrokes() const
    {
        if (!drawnStrokes)
        {
            drawnStrokes.emplace();
            for (auto const& layer : element->Elements(L"layer"))
            {
                for (auto const& e : layer->Elements(L"stroke"))
                {
                    if (Strokes::NeedsOwnDrawing(*e))
                    {
                        drawnStrokes->push_back(e);
                    }
                }
            }
        }
        return *drawnStrokes;
    }

    bool XoppPage::HasContent() const
    {
        bool shown = ink.IsAttached();
        for (auto const& layer : element->Elements(L"layer"))
        {
            for (auto const& e : layer->Elements())
            {
                // With a view, strokes are the ink's (the elements of erased strokes stay until written back).
                if (!shown || e->Name() != L"stroke" || Strokes::NeedsOwnDrawing(*e))
                {
                    return true;
                }
            }
        }
        return shown && !ink.View()->Strokes().empty();
    }

    void XoppPage::AddToTopLayer(std::vector<XmlElementPtr> const& elements)
    {
        auto top = element->Elements(L"layer").back();
        for (auto const& e : elements)
        {
            top->Append(e);
        }
    }

    XmlElementPtr XoppPage::Snapshot()
    {
        // Without view the element is the page as it is (a released view wrote its ink into it).
        return ink.IsAttached() ? ink.SnapshotOf(*ink.View()) : element->Clone();
    }
}
