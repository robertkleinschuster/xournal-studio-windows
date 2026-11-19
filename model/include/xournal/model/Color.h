#pragma once

// Colors as Xournal++ writes them: "#rrggbbaa" (and a few named colors of old Xournal files).

#include <cstdint>
#include <string>
#include <string_view>

namespace xournal
{
    struct Color
    {
        std::uint8_t a = 0xFF, r = 0, g = 0, b = 0; // the order of inkpages::Color
        bool operator==(Color const&) const = default;
    };

    namespace Colors
    {
        constexpr Color Black{ 0xFF, 0, 0, 0 };
        constexpr Color White{ 0xFF, 0xFF, 0xFF, 0xFF };
    }

    /// "#rrggbbaa", "#rrggbb" (opaque) or a named color of old Xournal files; `fallback` for anything else.
    Color ParseColor(std::wstring_view text, Color fallback);
    Color ParseColor(std::wstring const* text, Color fallback);

    /// "#rrggbbaa".
    std::wstring FormatColor(Color c);
}
