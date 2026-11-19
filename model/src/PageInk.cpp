#include "xournal/model/PageInk.h"

#include "ConvertedStrokes.h"
#include "xournal/model/Strokes.h"

#include <set>
#include <stdexcept>

namespace xournal
{
    PageInk::PageInk(XmlElement& page) : page(page), converted(std::make_unique<ConvertedStrokes>()) {}

    PageInk::~PageInk() = default;

    std::vector<InkRecord> PageInk::ReadStrokes() const
    {
        std::vector<InkRecord> strokes;
        for (auto const& layer : page.Elements(L"layer"))
        {
            for (auto const& e : layer->Elements(L"stroke"))
            {
                if (auto ink = Strokes::InkOf(*e))
                {
                    strokes.push_back({ std::move(*ink), e });
                }
            }
        }
        return strokes;
    }

    void PageInk::Attach(InkView const& ink, std::vector<std::pair<StrokeId, XmlElementPtr>> const& strokes)
    {
        if (view)
        {
            throw std::logic_error("PageInk::Attach: the page is already shown");
        }
        for (auto const& [id, element] : strokes)
        {
            strokeElements[id] = element;
        }
        view = &ink;
    }

    void PageInk::Detach()
    {
        if (!view)
        {
            throw std::logic_error("PageInk::Detach: the page is not shown");
        }
        WriteBack(*view);
        view = nullptr;
    }

    void PageInk::Adopt(StrokeId id, XmlElementPtr element) { strokeElements[id] = std::move(element); }

    XmlElement& PageInk::TopLayer() const
    {
        auto layers = page.Elements(L"layer");
        if (layers.empty())
        {
            throw std::logic_error("PageInk: a page always has a layer"); // XoppPage adds one when the file has none
        }
        return *layers.back();
    }

    Matrix PageInk::WrittenTransform(InkView const& ink, StrokeId id) const
    {
        auto committed = CommittedTransform(id);
        return committed ? *committed : ink.TransformOf(id);
    }

    void PageInk::WriteBack(InkView const& ink)
    {
        auto strokes = ink.Strokes();
        std::set<StrokeId> live(strokes.begin(), strokes.end());
        for (auto const& [id, e] : strokeElements)
        {
            if (!live.contains(id) && e->Parent())
            {
                e->Remove();
            }
        }
        XmlElement& topLayer = TopLayer();
        for (StrokeId id : strokes)
        {
            auto known = strokeElements.find(id);
            if (known == strokeElements.end())
            {
                topLayer.Append(converted->ElementOf(id, ink, WrittenTransform(ink, id)));
            }
            else if (!known->second->Parent())
            {
                topLayer.Append(known->second);
            }
        }
        strokeElements.clear();
        previewed.clear();
        converted->KeepOnly({});
    }

    XmlElementPtr PageInk::SnapshotOf(InkView const& ink)
    {
        auto strokes = ink.Strokes();
        std::set<StrokeId> live(strokes.begin(), strokes.end());
        std::unordered_map<XmlElement const*, StrokeId> idOf;
        for (auto const& [id, e] : strokeElements)
        {
            idOf[e.get()] = id;
        }
        auto erased = [&](XmlElement const* e)
        {
            auto it = idOf.find(e);
            return it != idOf.end() && !live.contains(it->second);
        };

        auto copy = page.CloneShallow();
        XmlElement* topLayer = nullptr;
        for (auto const& node : page.Nodes())
        {
            if (node.element && node.element->Name() == L"layer")
            {
                auto layerCopy = node.element->CloneShallow();
                for (auto const& child : node.element->Nodes())
                {
                    if (!child.element)
                    {
                        layerCopy->AppendNode(child);
                    }
                    else if (!erased(child.element.get()))
                    {
                        layerCopy->Append(child.element->Clone());
                    }
                }
                topLayer = layerCopy.get();
                copy->Append(std::move(layerCopy));
            }
            else if (node.element)
            {
                copy->Append(node.element->Clone());
            }
            else
            {
                copy->AppendNode(node);
            }
        }
        if (!topLayer)
        {
            throw std::logic_error("PageInk: a page always has a layer");
        }
        // Strokes added in this session follow in the top layer in drawing order, where Xournal draws by default too.
        for (StrokeId id : strokes)
        {
            auto known = strokeElements.find(id);
            if (known == strokeElements.end())
            {
                topLayer->Append(converted->ElementOf(id, ink, WrittenTransform(ink, id)));
            }
            else if (!known->second->Parent())
            {
                topLayer->Append(known->second->Clone()); // pasted or duplicated: its element
            }
        }
        converted->KeepOnly(live);
        return copy;
    }

    void PageInk::RenameStroke(StrokeId oldId, StrokeId newId)
    {
        auto node = strokeElements.extract(oldId);
        if (!node.empty())
        {
            strokeElements[newId] = std::move(node.mapped());
        }
        converted->Transfer(oldId, newId);
    }

    void PageInk::BeginPreview(StrokeId id, Matrix const& committed) { previewed[id] = committed; }

    std::optional<Matrix> PageInk::CommittedTransform(StrokeId id) const
    {
        auto it = previewed.find(id);
        return it == previewed.end() ? std::nullopt : std::optional<Matrix>(it->second);
    }

    void PageInk::EndPreview(std::vector<StrokeId> const& ids)
    {
        for (StrokeId id : ids)
        {
            previewed.erase(id);
        }
    }

    void PageInk::RecordTransform(StrokeId id, Matrix const& m)
    {
        auto it = strokeElements.find(id);
        if (it != strokeElements.end())
        {
            Strokes::Transform(*it->second, m);
        }
    }

    XmlElementPtr PageInk::ElementOf(StrokeId id) const
    {
        auto it = strokeElements.find(id);
        if (it != strokeElements.end())
        {
            return it->second->Clone();
        }
        if (!view)
        {
            throw std::logic_error("PageInk::ElementOf: a stroke without element needs the page's view");
        }
        return Strokes::ToXml(view->InkOf(id), view->TransformOf(id));
    }

    std::vector<StrokePlacement> PageInk::CapturePlacement(std::vector<StrokeId> const& strokes,
        std::function<Matrix(StrokeId)> const& transformOf) const
    {
        std::vector<StrokePlacement> placement;
        for (StrokeId id : strokes)
        {
            StrokePlacement p{ id, transformOf ? transformOf(id) : view ? view->TransformOf(id) : Matrix{}, std::nullopt };
            auto it = strokeElements.find(id);
            if (it != strokeElements.end())
            {
                p.coordinates = it->second->Value();
            }
            placement.push_back(std::move(p));
        }
        return placement;
    }

    void PageInk::RestorePlacement(std::vector<StrokePlacement> const& placement,
        std::vector<std::optional<StrokeId>> const& current)
    {
        for (size_t i = 0; i < current.size() && i < placement.size(); ++i)
        {
            if (!current[i] || !placement[i].coordinates)
            {
                continue;
            }
            auto it = strokeElements.find(*current[i]);
            if (it != strokeElements.end())
            {
                it->second->SetValue(*placement[i].coordinates);
            }
        }
    }

    int PageInk::Conversions() const { return converted->Conversions(); }
}
