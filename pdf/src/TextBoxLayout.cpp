#include "xournal/pdf/TextBoxLayout.h"

#include "xournal/base/Error.h"

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace xournal::pdf
{
    ComPtr<IDWriteTextLayout> TextBoxLayout(IDWriteFactory* factory, XoppText const& text, std::wstring const& family,
                                            std::wstring const& locale)
    {
        ComPtr<IDWriteTextFormat> format;
        Check("CreateTextFormat", factory->CreateTextFormat(
            family.c_str(), nullptr, text.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            text.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            static_cast<FLOAT>(text.size), locale.c_str(), &format));

        DWRITE_TEXT_ALIGNMENT alignment = text.alignment == TextAlignment::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                        : text.alignment == TextAlignment::Right  ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                                                   : DWRITE_TEXT_ALIGNMENT_LEADING;
        auto const length = static_cast<UINT32>(text.content.size());
        ComPtr<IDWriteTextLayout> layout;
        if (text.wrap)
        {
            Check("SetWordWrapping", format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
            if (text.justify) alignment = DWRITE_TEXT_ALIGNMENT_JUSTIFIED;
            Check("SetTextAlignment", format->SetTextAlignment(alignment));
            Check("CreateTextLayout", factory->CreateTextLayout(text.content.c_str(), length, format.Get(),
                                                                static_cast<FLOAT>(*text.wrap), 1e6f, &layout));
        }
        else
        {
            // One line per paragraph, aligned within the widest line (as the C# export).
            Check("SetWordWrapping", format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
            Check("CreateTextLayout", factory->CreateTextLayout(text.content.c_str(), length, format.Get(), 1e6f, 1e6f, &layout));
            DWRITE_TEXT_METRICS metrics{};
            Check("GetMetrics", layout->GetMetrics(&metrics));
            Check("SetMaxWidth", layout->SetMaxWidth(std::max(metrics.width, 1.0f)));
            Check("SetTextAlignment", layout->SetTextAlignment(alignment));
        }
        return layout;
    }
}
