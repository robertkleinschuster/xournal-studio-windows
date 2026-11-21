#include "TextDrawing.h"

#include "Direct2D.h"

#include "xournal/base/Error.h"

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace xournal::app
{
    namespace
    {
        constexpr float Unbounded = 100000; // no wrap: one line per paragraph
    }

    TextDrawing::TextDrawing()
    {
        Check("DWriteCreateFactory", DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory.GetAddressOf())));
    }

    ComPtr<IDWriteTextLayout> TextDrawing::Layout(XoppText const& text) const
    {
        ComPtr<IDWriteTextFormat> format;
        Check("IDWriteFactory::CreateTextFormat", factory->CreateTextFormat(text.WindowsFamily().c_str(), nullptr,
            text.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            text.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            static_cast<float>(text.size), L"de-de", &format));
        format->SetWordWrapping(text.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);

        float width = text.wrap ? static_cast<float>(*text.wrap) : Unbounded;
        ComPtr<IDWriteTextLayout> layout;
        Check("IDWriteFactory::CreateTextLayout", factory->CreateTextLayout(text.content.c_str(),
            static_cast<UINT32>(text.content.size()), format.Get(), width, Unbounded, &layout));
        if (!text.wrap)
        {
            // Alignment is within the box: without a wrap width the box is as wide as the longest line.
            DWRITE_TEXT_METRICS metrics{};
            Check("IDWriteTextLayout::GetMetrics", layout->GetMetrics(&metrics));
            layout->SetMaxWidth(metrics.widthIncludingTrailingWhitespace);
        }
        DWRITE_TEXT_ALIGNMENT alignment = text.justify ? DWRITE_TEXT_ALIGNMENT_JUSTIFIED
            : text.alignment == TextAlignment::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
            : text.alignment == TextAlignment::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING;
        layout->SetTextAlignment(alignment);
        return layout;
    }

    void TextDrawing::Draw(ID2D1DeviceContext* dc, XoppText const& text, Matrix const& extra)
    {
        auto layout = Layout(text);
        ComPtr<ID2D1SolidColorBrush> brush;
        Check("CreateSolidColorBrush (text)", dc->CreateSolidColorBrush(ToD2D(text.color), &brush));
        D2D1_MATRIX_3X2_F page;
        dc->GetTransform(&page);
        dc->SetTransform(ToD2D(text.transform * extra) * page);
        dc->DrawTextLayout({ 0, 0 }, layout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        dc->SetTransform(page);
    }

    Rect TextDrawing::LocalBounds(XoppText const& text) const
    {
        DWRITE_TEXT_METRICS metrics{};
        Check("IDWriteTextLayout::GetMetrics", Layout(text)->GetMetrics(&metrics));
        double width = std::max<double>(metrics.widthIncludingTrailingWhitespace, text.size / 2);
        double height = std::max<double>(metrics.height, text.size);
        double left = text.wrap ? 0 : metrics.left;
        return { left, 0, width, height };
    }

    Rect TextDrawing::Bounds(XoppText const& text) const
    {
        Rect b = LocalBounds(text);
        Point corners[] = { text.transform.Apply({ b.Left(), b.Top() }), text.transform.Apply({ b.Right(), b.Top() }),
            text.transform.Apply({ b.Left(), b.Bottom() }), text.transform.Apply({ b.Right(), b.Bottom() }) };
        double x0 = corners[0].x, y0 = corners[0].y, x1 = x0, y1 = y0;
        for (auto c : corners)
        {
            x0 = std::min(x0, c.x); y0 = std::min(y0, c.y); x1 = std::max(x1, c.x); y1 = std::max(y1, c.y);
        }
        return { x0, y0, x1 - x0, y1 - y0 };
    }
}
