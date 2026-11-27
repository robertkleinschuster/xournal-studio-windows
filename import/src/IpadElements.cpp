#include "IpadElements.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/model/XoppFormat.h"
#include "xournal/pdf/TextBoxLayout.h"

#include <qpdf/BufferInputSource.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <span>

using Microsoft::WRL::ComPtr;

namespace xournal::import::detail
{
    std::string Num(double v) { return std::format("{:.3f}", v); }

    std::string PdfDate()
    {
        SYSTEMTIME t{};
        GetSystemTime(&t);
        return std::format("D:{:04}{:02}{:02}{:02}{:02}{:02}Z00'00'", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    }

    namespace
    {
        using Handle = QPDFObjectHandle;

        Handle Real(double v) { return Handle::newReal(v, 3); }

        Handle Array(std::initializer_list<double> values)
        {
            auto array = Handle::newArray();
            for (double v : values) array.appendItem(Real(v));
            return array;
        }

        Handle UnicodeString(std::wstring const& text) { return Handle::newUnicodeString(ToUtf8(text)); }

        std::string NewName()
        {
            std::mt19937_64 random{ std::random_device{}() };
            return std::format("{:016X}{:016X}", random(), random());
        }

        // ---- boxes (page coordinates, y down)

        Rect Union(Rect a, Rect b)
        {
            double l = std::min(a.x, b.x), t = std::min(a.y, b.y), r = std::max(a.x + a.width, b.x + b.width), bt = std::max(a.y + a.height, b.y + b.height);
            return { l, t, r - l, bt - t };
        }

        Rect StrokeBox(StrokeShape const& s)
        {
            double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30, widest = s.width;
            for (double w : s.segmentWidths) widest = std::max(widest, w);
            for (auto const& p : s.points)
            {
                minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            }
            double pad = widest + 1; // square ends reach 0.71 of the width beyond a point; a fill is the line's own outline
            return { minX - pad, minY - pad, maxX - minX + 2 * pad, maxY - minY + 2 * pad };
        }

        IDWriteFactory* DirectWrite()
        {
            static ComPtr<IDWriteFactory> factory = [] {
                ComPtr<IDWriteFactory> f;
                Check("DWriteCreateFactory", DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                                 reinterpret_cast<IUnknown**>(f.GetAddressOf())));
                return f;
            }();
            return factory.Get();
        }

        // The text's box as the export lays it out (its own layout), placed by its transform.
        Rect TextBox(XoppText const& text)
        {
            std::wstring family = text.WindowsFamily();
            auto layout = pdf::TextBoxLayout(DirectWrite(), text, family.empty() ? L"Arial" : family, L"de-DE");
            DWRITE_TEXT_METRICS metrics{};
            Check("IDWriteTextLayout::GetMetrics", layout->GetMetrics(&metrics));
            double width = std::max<double>(metrics.widthIncludingTrailingWhitespace, 1), height = std::max<double>(metrics.height, 1);
            Rect box{ 0, 0, 0, 0 };
            bool first = true;
            for (Point corner : { Point{ 0, 0 }, Point{ width, 0 }, Point{ 0, height }, Point{ width, height } })
            {
                Point p = text.transform.Apply(corner);
                Rect point{ p.x, p.y, 0, 0 };
                box = first ? point : Union(box, point);
                first = false;
            }
            return { box.x - 1, box.y - 1, box.width + 2, box.height + 2 };
        }

        // ---- the appearance: the element alone on a transparent one-page PDF of its box, as a form XObject

        pdf::PageElement Shifted(pdf::PageElement element, double dx, double dy)
        {
            if (auto* s = std::get_if<StrokeShape>(&element))
            {
                for (auto& p : s->points)
                {
                    p.x += dx;
                    p.y += dy;
                }
            }
            else if (auto* t = std::get_if<XoppText>(&element))
            {
                t->transform = t->transform * Matrix::Translation(dx, dy);
            }
            else if (auto* p = std::get_if<pdf::Picture>(&element))
            {
                p->placement.transform = p->placement.transform * Matrix::Translation(dx, dy);
            }
            return element;
        }

        Handle Appearance(QPDF& pdf, ForeignKeep& keep, pdf::PageElement const& element, Rect box, pdf::ExportOptions const& options)
        {
            pdf::ExportPage page;
            page.width = box.width;
            page.height = box.height;
            page.background = pdf::SolidBackground{ Color{ 0, 255, 255, 255 }, BackgroundStyle::Plain, std::nullopt }; // transparent paper
            page.elements.push_back(Shifted(element, -box.x, -box.y));
            pdf::ExportResult result;
            auto bytes = pdf::ExportToMemory(std::span<pdf::ExportPage const>(&page, 1), result, options);
            auto mini = std::make_shared<QPDF>();
            mini->setSuppressWarnings(true);
            // The source owns a copy of the bytes, and `keep` the document: the copied streams are read when `pdf` is written.
            mini->processInputSource(std::make_shared<BufferInputSource>("appearance", std::string(bytes.begin(), bytes.end())));
            keep.push_back(mini);
            auto pages = QPDFPageDocumentHelper(*mini).getAllPages();
            if (pages.empty()) ThrowInvalidData("iPad export: appearance without a page");
            return pdf.copyForeignObject(pages[0].getFormXObjectForPage());
        }

        // ---- what the annotation carries of the element

        Handle Original(XmlElement const& origin, Rect box, double pageHeight)
        {
            auto custom = Handle::newDictionary();
            custom.replaceKey("/Element", UnicodeString(origin.Name()));
            auto attributes = Handle::newArray();
            for (auto const& attribute : origin.Attributes())
            {
                attributes.appendItem(UnicodeString(attribute.name));
                attributes.appendItem(UnicodeString(attribute.value));
            }
            custom.replaceKey("/Attrs", attributes);
            custom.replaceKey("/Value", UnicodeString(origin.Value()));
            custom.replaceKey("/Rect0", Array({ box.x, pageHeight - box.y - box.height, box.x + box.width, pageHeight - box.y }));
            return custom;
        }

        void Append(QPDFObjectHandle page, Handle annotation)
        {
            auto annots = page.getKey("/Annots");
            if (!annots.isArray())
            {
                annots = Handle::newArray();
                page.replaceKey("/Annots", annots);
            }
            annots.appendItem(annotation);
        }

        // The keys every annotation of these gets.
        Handle Annotation(char const* subtype, Rect box, double pageHeight, Handle appearance)
        {
            auto a = Handle::newDictionary();
            a.replaceKey("/Type", Handle::newName("/Annot"));
            a.replaceKey("/Subtype", Handle::newName(subtype));
            a.replaceKey("/Rect", Array({ box.x, pageHeight - box.y - box.height, box.x + box.width, pageHeight - box.y }));
            a.replaceKey("/F", Handle::newInteger(4)); // print
            a.replaceKey("/T", Handle::newUnicodeString("Xournal Studio"));
            a.replaceKey("/M", Handle::newString(PdfDate()));
            a.replaceKey("/NM", Handle::newUnicodeString(NewName()));
            a.replaceKey("/Border", Array({ 0, 0, 0 }));
            a.replaceKey("/AP", Handle::newDictionary({ { "/N", appearance } }));
            return a;
        }

        void AddStroke(QPDF& pdf, ForeignKeep& keep, QPDFObjectHandle page, double pageHeight, StrokeShape const& s, XmlElement const& origin,
                       pdf::ExportOptions const& options)
        {
            Rect box = StrokeBox(s);
            auto a = Annotation("/Ink", box, pageHeight, pdf.makeIndirectObject(Appearance(pdf, keep, s, box, options)));
            auto line = Handle::newArray();
            for (auto const& p : s.points)
            {
                line.appendItem(Real(p.x));
                line.appendItem(Real(pageHeight - p.y));
            }
            auto list = Handle::newArray();
            list.appendItem(line);
            a.replaceKey("/InkList", list);
            a.replaceKey("/C", Array({ s.color.r / 255.0, s.color.g / 255.0, s.color.b / 255.0 }));
            a.replaceKey("/CA", Real(s.color.a / 255.0));
            auto style = Handle::newDictionary();
            style.replaceKey("/W", Real(s.width));
            if (!s.dashes.empty())
            {
                auto dash = Handle::newArray();
                for (double d : s.dashes) dash.appendItem(Real(d));
                style.replaceKey("/S", Handle::newName("/D"));
                style.replaceKey("/D", dash);
            }
            a.replaceKey("/BS", style);
            a.replaceKey("/XournalStudio", Original(origin, box, pageHeight));
            Append(page, pdf.makeIndirectObject(a));
        }

        void AddText(QPDF& pdf, ForeignKeep& keep, QPDFObjectHandle page, double pageHeight, XoppText const& text, XmlElement const& origin,
                     pdf::ExportOptions const& options)
        {
            Rect box = TextBox(text);
            auto a = Annotation("/FreeText", box, pageHeight, pdf.makeIndirectObject(Appearance(pdf, keep, text, box, options)));
            std::wstring content = text.content;
            std::replace(content.begin(), content.end(), L'\n', L'\r'); // PDF's line end in text strings
            a.replaceKey("/Contents", UnicodeString(content));
            a.replaceKey("/DA", Handle::newString(std::format("/Helv {} Tf {} {} {} rg", Num(text.size), Num(text.color.r / 255.0),
                                                              Num(text.color.g / 255.0), Num(text.color.b / 255.0))));
            a.replaceKey("/XournalStudio", Original(origin, box, pageHeight));
            Append(page, pdf.makeIndirectObject(a));
        }
    }

    void AddElementAnnotation(QPDF& pdf, ForeignKeep& keep, QPDFObjectHandle page, double pageHeight, pdf::PageElement const& element,
                              XmlElementPtr const& origin, pdf::ExportOptions const& options)
    {
        if (!origin) ThrowInvalidData("iPad export: an element without its source");
        if (auto const* stroke = std::get_if<StrokeShape>(&element)) AddStroke(pdf, keep, page, pageHeight, *stroke, *origin, options);
        else if (auto const* text = std::get_if<XoppText>(&element)) AddText(pdf, keep, page, pageHeight, *text, *origin, options);
        else ThrowInvalidData("iPad export: this element has no annotation yet");
    }

    // ================================================================== reading

    namespace
    {
        std::vector<double> Numbers(std::wstring_view text)
        {
            std::vector<double> numbers;
            size_t at = 0;
            while (at < text.size())
            {
                size_t end = text.find(L' ', at);
                if (end == std::wstring_view::npos) end = text.size();
                if (end > at) numbers.push_back(XoppFormat::ParseDouble(std::wstring(text.substr(at, end - at))).value_or(0));
                at = end + 1;
            }
            return numbers;
        }

        std::wstring Format(double v) { return XoppFormat::FormatNumber(v, 3); }

        // "#rrggbb" or "#rrggbbaa" -> rgba bytes.
        bool ParseColor(std::wstring const& text, int (&rgba)[4])
        {
            if (text.size() != 7 && text.size() != 9) return false;
            if (text[0] != L'#') return false;
            rgba[3] = 255;
            for (size_t i = 0; i < (text.size() - 1) / 2; ++i)
            {
                try
                {
                    rgba[i] = std::stoi(text.substr(1 + 2 * i, 2), nullptr, 16);
                }
                catch (std::exception const&)
                {
                    return false; // not a hex color: left as it is
                }
            }
            return true;
        }

        std::wstring ColorText(int r, int g, int b, int a) { return std::format(L"#{:02x}{:02x}{:02x}{:02x}", r, g, b, a); }

        std::wstring Attribute(XmlElement const& e, std::wstring const& name) { return e.AttributeOr(name, L""); }
    }

    XmlElementPtr ReadElementAnnotation(QPDFObjectHandle a, pdf::PageFrame const& frame)
    {
        auto custom = a.getKey("/XournalStudio");
        if (!custom.isDictionary() || !custom.getKey("/Element").isString() || !custom.getKey("/Attrs").isArray()) return nullptr;
        auto element = XmlElement::Make(FromUtf8(custom.getKey("/Element").getUTF8Value()));
        auto attributes = custom.getKey("/Attrs");
        for (int i = 0; i + 1 < attributes.getArrayNItems(); i += 2)
        {
            element->SetAttribute(FromUtf8(attributes.getArrayItem(i).getUTF8Value()), FromUtf8(attributes.getArrayItem(i + 1).getUTF8Value()));
        }
        std::wstring stored = custom.getKey("/Value").isString() ? FromUtf8(custom.getKey("/Value").getUTF8Value()) : std::wstring();
        element->SetValue(stored);

        // How far the iPad moved the annotation (its rectangle against the one it was written with), as shown on the page.
        double dx = 0, dy = 0;
        auto rect0 = custom.getKey("/Rect0"), rect = a.getKey("/Rect");
        if (rect0.isRectangle() && rect.isRectangle())
        {
            auto r0 = rect0.getArrayAsRectangle(), r1 = rect.getArrayAsRectangle();
            dx = std::min(r1.llx, r1.urx) - std::min(r0.llx, r0.urx);
            dy = -(std::max(r1.lly, r1.ury) - std::max(r0.lly, r0.ury));
        }
        if (std::abs(dx) < 0.01) dx = 0;
        if (std::abs(dy) < 0.01) dy = 0;

        std::wstring name = element->Name();
        if (name == L"stroke")
        {
            auto list = a.getKey("/InkList");
            if (list.isArray() && list.getArrayNItems() > 0 && list.getArrayItem(0).isArray())
            {
                auto line = list.getArrayItem(0);
                std::vector<double> shown;
                for (int i = 0; i + 1 < line.getArrayNItems(); i += 2)
                {
                    Point p = frame.FromUserSpace(line.getArrayItem(i).getNumericValue(), line.getArrayItem(i + 1).getNumericValue());
                    shown.push_back(p.x);
                    shown.push_back(p.y);
                }
                auto original = Numbers(stored);
                bool same = original.size() == shown.size();
                for (size_t i = 0; same && i < shown.size(); ++i) same = std::abs(original[i] - shown[i]) <= 0.01;
                if (!same && !shown.empty())
                {
                    std::wstring text;
                    for (size_t i = 0; i < shown.size(); ++i) text += (i ? L" " : L"") + Format(shown[i]);
                    element->SetValue(text);
                }
            }
            auto c = a.getKey("/C");
            int rgba[4];
            if (c.isArray() && c.getArrayNItems() == 3 && ParseColor(Attribute(*element, L"color"), rgba))
            {
                int now[3];
                for (int i = 0; i < 3; ++i) now[i] = static_cast<int>(std::lround(std::clamp(c.getArrayItem(i).getNumericValue(), 0.0, 1.0) * 255));
                if (now[0] != rgba[0] || now[1] != rgba[1] || now[2] != rgba[2])
                    element->SetAttribute(L"color", ColorText(now[0], now[1], now[2], rgba[3]));
            }
            auto width = a.getKey("/BS").getKey("/W");
            auto widths = Numbers(Attribute(*element, L"width"));
            if (width.isNumber() && !widths.empty() && std::abs(width.getNumericValue() - widths[0]) > 0.01)
                element->SetAttribute(L"width", Format(width.getNumericValue())); // the iPad changed it (pressure goes)
        }
        else if (name == L"text")
        {
            auto contents = a.getKey("/Contents");
            if (contents.isString())
            {
                std::wstring text = FromUtf8(contents.getUTF8Value());
                std::wstring normal;
                for (size_t i = 0; i < text.size(); ++i)
                {
                    if (text[i] == L'\r')
                    {
                        normal += L'\n';
                        if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
                    }
                    else normal += text[i];
                }
                element->SetValue(normal);
            }
            if (dx != 0 || dy != 0)
            {
                auto x = XoppFormat::ParseDouble(Attribute(*element, L"x")), y = XoppFormat::ParseDouble(Attribute(*element, L"y"));
                if (x && y)
                {
                    element->SetAttribute(L"x", Format(*x + dx));
                    element->SetAttribute(L"y", Format(*y + dy));
                }
            }
        }
        return element;
    }
}
