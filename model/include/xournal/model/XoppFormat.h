#pragma once

// The .xopp format of upstream Xournal++ (src/core/control/xojfile: XmlTags.h, XmlAttrs.h, SaveHandler.cpp). Every
// feature is expressed with upstream elements and attributes; the app invents none and keeps everything it reads.

#include "xournal/model/Geometry.h"
#include "xournal/model/Xml.h"

#include <optional>
#include <string>
#include <string_view>

namespace xournal::XoppFormat
{
    /// File version written for new documents and for files that have none (Xournal's): 4, what the released Xournal++
    /// 1.2 writes (upstream's development line writes 5, which only adds the "matrix" of texts/pictures). Everything the
    /// app writes itself is version 4 (pictures by left/top/right/bottom). A loaded file keeps its version: upstream
    /// reads audio timestamps of versions below 4 as seconds, so raising it would misread them; elements with a
    /// "matrix" copied into a version-4 file read the same in every Xournal++ whatever the number says. The C# app
    /// does the same.
    inline constexpr std::wstring_view FileVersion = L"4";

    /// The creator attribute; upstream only stores it (every save writes ours, as upstream writes its own).
    inline constexpr std::wstring_view Creator = L"Xournal Studio for Windows";

    inline constexpr std::wstring_view Title = L"Xournal++ document - see https://xournalpp.github.io/";

    /// A4 in points, upstream's default page.
    inline constexpr double A4Width = 595.27559, A4Height = 841.88976;

    /// Largest magnitude a number of a file may have: pages are a few hundred points, so a value beyond a million
    /// cannot be meant (damaged file). Such values are not shown, but kept in the file.
    inline constexpr double NumberLimit = 1'000'000;

    /// A number as .NET's double.Parse reads it (invariant culture, optional sign, exponent), or nothing.
    std::optional<double> ParseDouble(std::wstring_view text);

    /// An integer (optional sign, surrounding whitespace allowed), or nothing.
    std::optional<int> ParseInt(std::wstring_view text);

    /// A number of the file, or nothing when it is missing, not a number, or beyond NumberLimit - the one rule for the
    /// numbers of texts, pictures and strokes.
    std::optional<double> ReadNumber(std::wstring_view text);
    std::optional<double> ReadNumber(std::wstring const* text);

    /// An attribute's number (see ReadNumber) or `fallback`.
    double ReadNumber(XmlElement const& e, std::wstring_view name, double fallback);

    /// The "matrix" attribute of file version 5 (texts, pictures): xx yx xy yy x0 y0, cairo's order.
    std::optional<Matrix> ReadMatrix(XmlElement const& e);
    void WriteMatrix(XmlElement& e, Matrix const& m);

    /// A number with at most `decimals` decimals, trailing zeros dropped (.NET's "0.####" for 4).
    std::wstring FormatNumber(double value, int decimals = 4);

    /// The shortest text that reads back as exactly this number (as XLinq writes a double attribute).
    std::wstring FormatShortest(double value);
}
