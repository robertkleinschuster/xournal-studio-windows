#pragma once

// A text of a page as upstream Xournal++ reads it (XmlParser::parseTextTag): <text font="Sans" size="12" color="#..."
// x y | matrix [wrap] [align] [justify]>content</text>; <link> (a text with a url) is shown the same way. The font is a
// Pango description ("Sans Bold Italic"). Read only: the app shows texts, it does not edit them.

#include "xournal/model/Color.h"
#include "xournal/model/Geometry.h"
#include "xournal/model/Xml.h"

#include <optional>
#include <string>
#include <vector>

namespace xournal
{
    enum class TextAlignment { Left, Center, Right };

    struct XoppText
    {
        std::wstring content;
        std::wstring family;
        bool bold = false;
        bool italic = false;
        double size = 12;
        Color color = Colors::Black;
        Matrix transform;            // places the box's top left (x, y or the matrix)
        std::optional<double> wrap;  // width of a wrapped text
        TextAlignment alignment = TextAlignment::Left;
        bool justify = false;

        static bool IsText(XmlElement const& e);
        static XoppText Read(XmlElement const& e);

        /// The paragraphs: split where DirectWrite starts a new line (CR LF, LF, CR, NEL, LS, PS).
        std::vector<std::wstring> Paragraphs() const;

        /// The Windows font family for a Pango family name (Pango's aliases on Windows).
        std::wstring WindowsFamily() const;

        /// Characters as a reader counts them (user-perceived: an emoji or a letter with its accents is one), by the
        /// ICU that ships with Windows.
        static size_t CharacterCount(std::wstring_view text);
    };
}
