#include "TextDrawing.h"

#include "Direct2D.h"

#include "xournal/base/Error.h"

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

    void TextDrawing::Draw(ID2D1DeviceContext* dc, XoppText const& text)
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

        ComPtr<ID2D1SolidColorBrush> brush;
        Check("CreateSolidColorBrush (text)", dc->CreateSolidColorBrush(ToD2D(text.color), &brush));
        D2D1_MATRIX_3X2_F page;
        dc->GetTransform(&page);
        dc->SetTransform(ToD2D(text.transform) * page);
        dc->DrawTextLayout({ 0, 0 }, layout.Get(), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        dc->SetTransform(page);
    }
}
