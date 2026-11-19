#include "ConvertedStrokes.h"

#include "xournal/model/Strokes.h"

namespace xournal
{
    XmlElementPtr ConvertedStrokes::ElementOf(StrokeId id, InkView const& ink, Matrix const& transform)
    {
        auto it = elements.find(id);
        if (it == elements.end() || it->second.transform != transform)
        {
            Converted converted{ transform, Strokes::ToXml(ink.InkOf(id), transform) };
            it = elements.insert_or_assign(id, std::move(converted)).first;
            ++conversions;
        }
        return it->second.element->Clone();
    }

    void ConvertedStrokes::Transfer(StrokeId oldId, StrokeId newId)
    {
        auto node = elements.extract(oldId);
        if (!node.empty())
        {
            node.key() = newId;
            elements.insert_or_assign(newId, std::move(node.mapped()));
        }
    }

    void ConvertedStrokes::KeepOnly(std::set<StrokeId> const& live)
    {
        std::erase_if(elements, [&](auto const& entry) { return !live.contains(entry.first); });
    }
}
