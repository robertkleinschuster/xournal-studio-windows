#include "xournal/model/PageImages.h"

#include "xournal/model/XoppImage.h"

#include <algorithm>
#include <stdexcept>

namespace xournal
{
    std::vector<XmlElementPtr> PageImages::List() const
    {
        std::vector<XmlElementPtr> images;
        for (auto const& layer : page.Elements(L"layer"))
        {
            for (auto const& e : layer->Elements())
            {
                if (XoppImage::IsImage(*e))
                {
                    images.push_back(e);
                }
            }
        }
        return images;
    }

    void PageImages::Add(std::vector<XmlElementPtr> const& images)
    {
        auto layers = page.Elements(L"layer");
        if (layers.empty())
        {
            throw std::logic_error("PageImages: a page always has a layer");
        }
        for (auto const& image : images)
        {
            layers.back()->Append(image);
        }
        RaiseChanged(images);
    }

    std::vector<ImagePlace> PageImages::Remove(std::vector<XmlElementPtr> const& images)
    {
        std::vector<ImagePlace> places;
        for (auto const& image : images)
        {
            if (XmlElement* layer = image->Parent())
            {
                // The layer is shared by the page's tree: find its owning pointer to keep it in the place.
                for (auto const& candidate : page.Elements(L"layer"))
                {
                    if (candidate.get() == layer)
                    {
                        places.push_back({ image, candidate, layer->IndexOf(image.get()) });
                    }
                }
            }
        }
        for (auto const& place : places)
        {
            place.image->Remove();
        }
        RaiseChanged(images);
        return places;
    }

    void PageImages::Restore(std::vector<ImagePlace> const& places)
    {
        auto ordered = places;
        std::stable_sort(ordered.begin(), ordered.end(), [](auto const& a, auto const& b) { return a.index < b.index; });
        std::vector<XmlElementPtr> images;
        for (auto const& place : ordered)
        {
            auto children = place.layer->Elements();
            if (place.index < static_cast<int>(children.size()))
            {
                place.layer->InsertBefore(children[place.index].get(), place.image);
            }
            else
            {
                place.layer->Append(place.image);
            }
            images.push_back(place.image);
        }
        RaiseChanged(images);
    }

    void PageImages::Transform(std::vector<XmlElementPtr> const& images, Matrix const& m)
    {
        for (auto const& image : images)
        {
            XoppImage::SetBounds(*image, XoppImage::Transform(XoppImage::GetBounds(*image), m));
        }
        if (moved)
        {
            moved(images);
        }
    }

    std::vector<ImageBounds> PageImages::Capture(std::vector<XmlElementPtr> const& images) const
    {
        std::vector<ImageBounds> bounds;
        for (auto const& image : images)
        {
            bounds.push_back({ image, XoppImage::GetBounds(*image) });
        }
        return bounds;
    }

    void PageImages::Restore(std::vector<ImageBounds> const& bounds)
    {
        std::vector<XmlElementPtr> images;
        for (auto const& [image, rect] : bounds)
        {
            XoppImage::SetBounds(*image, rect);
            images.push_back(image);
        }
        if (!images.empty() && moved)
        {
            moved(images);
        }
    }

    void PageImages::RaiseChanged(std::vector<XmlElementPtr> const& images) const
    {
        if (!images.empty() && changed)
        {
            changed(images);
        }
    }
}
