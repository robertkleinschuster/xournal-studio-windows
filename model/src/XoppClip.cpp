#include "xournal/model/XoppClip.h"

#include "xournal/model/Strokes.h"
#include "xournal/model/XoppFormat.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppText.h"

namespace xournal::XoppClip
{
    XmlElementPtr Create(Rect bounds, std::vector<XmlElementPtr> const& elements)
    {
        auto clip = XmlElement::Make(L"clip", {
            { L"width", XoppFormat::FormatNumber(bounds.width) },
            { L"height", XoppFormat::FormatNumber(bounds.height) },
        });
        Matrix toOrigin = Matrix::Translation(-bounds.x, -bounds.y);
        for (auto const& e : elements)
        {
            clip->Append(Transformed(*e, toOrigin));
        }
        return clip;
    }

    Size GetSize(XmlElement const& clip)
    {
        return { XoppFormat::ParseDouble(clip.AttributeOr(L"width", L"0")).value_or(0),
            XoppFormat::ParseDouble(clip.AttributeOr(L"height", L"0")).value_or(0) };
    }

    std::vector<XmlElementPtr> Place(XmlElement const& clip, Matrix const& m)
    {
        std::vector<XmlElementPtr> placed;
        for (auto const& e : clip.Elements())
        {
            placed.push_back(Transformed(*e, m));
        }
        return placed;
    }

    XmlElementPtr Transformed(XmlElement const& e, Matrix const& m)
    {
        auto copy = e.Clone();
        if (copy->Name() == L"stroke")
        {
            Strokes::Transform(*copy, m);
        }
        else if (XoppImage::IsImage(*copy))
        {
            XoppImage::SetBounds(*copy, XoppImage::Transform(XoppImage::GetBounds(*copy), m));
        }
        else if (XoppText::IsText(*copy))
        {
            XoppText::Transform(*copy, m);
        }
        return copy;
    }
}
