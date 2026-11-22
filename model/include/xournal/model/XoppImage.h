#pragma once

// Xournal's picture elements: <image> (PNG/JPEG) and <teximage> (LaTeX or copied PDF areas, a one-page PDF or PNG), the
// data as base64 content, placed by left/top/right/bottom (file version 4) or by a "matrix" from the picture's natural
// size in pixels to the page (version 5; then natural_size is written too). New pictures are written as plain <image>
// with left/top/right/bottom, which every Xournal++ reads; a picture placed by a matrix keeps its matrix when moved.

#include "xournal/model/Geometry.h"
#include "xournal/model/Xml.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace xournal::XoppImage
{
    XmlElementPtr CreatePdf(Rect bounds, std::span<std::uint8_t const> pdf);
    bool IsImage(XmlElement const& e);

    XmlElementPtr Create(Rect bounds, std::span<std::uint8_t const> png, int pixelWidth, int pixelHeight);

    /// How the picture is drawn: its data stretched over `size`, then placed by `transform` (version 4: the bounds'
    /// size moved to their corner; version 5: natural size and matrix).
    struct Placement
    {
        Size size;
        Matrix transform;
    };

    Placement GetPlacement(XmlElement const& e);

    /// The area the picture covers on the page (axis-aligned, also for a turned matrix).
    Rect GetBounds(XmlElement const& e);

    /// Moves/scales the element to cover `bounds` (all other attributes kept); a matrix gets the mapping from the old
    /// bounds to the new ones added.
    void SetBounds(XmlElement& e, Rect bounds);

    /// The bounds after scaling/moving by `m` (pictures stay upright).
    std::optional<Size> NaturalSize(XmlElement const& e);
    void Transform(XmlElement& e, Matrix const& m, std::optional<Size> intrinsic);
    Rect Transform(Rect bounds, Matrix const& m);

    /// The picture's data (base64 content decoded by Windows' CryptStringToBinary); empty when it is not base64 - the
    /// picture then shows nothing, as before.
    std::vector<std::uint8_t> GetData(XmlElement const& e);

    bool IsPdf(std::span<std::uint8_t const> data);
}
