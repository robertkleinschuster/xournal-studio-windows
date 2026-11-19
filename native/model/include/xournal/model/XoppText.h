#pragma once

// A text of a page as upstream Xournal++ reads it (XmlParser::parseTextTag): <text font="Sans" size="12" color="#..."
// x y | matrix [wrap] [align] [justify]>content</text>; <link> (a text with a url) is shown the same way. The font is a
// Pango description ("Sans Bold Italic"). The Text tool writes new texts as upstream does (font size x y color) and
// changes only what was edited (content, font, size, color, place); moving and scaling a text moves its top left and
// scales its size (and wrap width), a text placed by a matrix gets the move added to its matrix.

#include "xournal/model/Color.h"
#include "xournal/model/Geometry.h"
#include "xournal/model/Xml.h"

#include <optional>
#include <string>
#include <vector>

namespace xournal
{
    enum class TextAlignment { Left, Center, Right };

    /// What the Text tool edits: the content (\n between lines), a Windows font family, size, color, the top left (page
    /// units).
    struct TextEdit
    {
        std::wstring content;
        std::wstring windowsFamily;
        double size = 12;
        Color color = Colors::Black;
        Point at;
    };

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

        /// A new <text> as upstream writes it.
        static XmlElementPtr Create(TextEdit const& edit);
        /// Writes an edit into a text: only what differs from it now (other attributes stay; the font description too
        /// while its family is the edited one).
        static void Apply(XmlElement& e, TextEdit const& edit);
        /// Moves/scales a text by `m` (page units): its top left (or matrix) follows, size and wrap width scale.
        static void Transform(XmlElement& e, Matrix const& m);
        /// The edit a text is now (its content with \n line ends).
        TextEdit ToEdit() const;

        /// The Pango family written for a Windows family (Arial = Xournal++'s default "Sans").
        static std::wstring PangoFamily(std::wstring const& windowsFamily);

        /// The paragraphs: split where DirectWrite starts a new line (CR LF, LF, CR, NEL, LS, PS).
        std::vector<std::wstring> Paragraphs() const;

        /// The Windows font family for a Pango family name (Pango's aliases on Windows).
        std::wstring WindowsFamily() const;

        /// Characters as a reader counts them (user-perceived: an emoji or a letter with its accents is one), by the
        /// ICU that ships with Windows.
        static size_t CharacterCount(std::wstring_view text);
    };
}
