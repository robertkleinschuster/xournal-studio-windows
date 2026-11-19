#pragma once

// A group of .xopp elements (<stroke>, <image>, <teximage>) relative to its top-left corner: what is copied to the
// clipboard and pasted or duplicated. Lives only on the clipboard, never in a file; the elements in it are upstream's.

#include "xournal/model/Geometry.h"
#include "xournal/model/Xml.h"

#include <vector>

namespace xournal::XoppClip
{
    /// Copies of the elements moved so that `bounds`' top-left corner is at 0,0.
    XmlElementPtr Create(Rect bounds, std::vector<XmlElementPtr> const& elements);

    Size GetSize(XmlElement const& clip);

    /// Copies of the elements, placed and scaled by `m`.
    std::vector<XmlElementPtr> Place(XmlElement const& clip, Matrix const& m);

    /// A copy of a stroke or picture element moved/scaled by `m`.
    XmlElementPtr Transformed(XmlElement const& e, Matrix const& m);
}
