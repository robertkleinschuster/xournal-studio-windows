#pragma once

// A page's pictures (<image>, <teximage>): listed bottom to top, added to the top layer, removed and put back exactly at
// their place (the file keeps its layers and order), moved and scaled in place. The page view follows through the two
// callbacks.

#include "xournal/model/Geometry.h"
#include "xournal/model/Xml.h"

#include <functional>
#include <vector>

namespace xournal
{
    /// Where a removed picture was: its layer and its position among the layer's elements.
    struct ImagePlace
    {
        XmlElementPtr image;
        XmlElementPtr layer;
        int index = 0;
    };

    /// Where pictures were (undo/redo of a move or scale).
    struct ImageBounds
    {
        XmlElementPtr image;
        Rect bounds;
    };

    class PageImages
    {
    public:
        /// `page`: the page's element (owned by its XoppPage, which owns this).
        explicit PageImages(XmlElement& page) : page(page) {}

        /// Pictures of all layers, bottom to top.
        std::vector<XmlElementPtr> List() const;

        /// Adds pictures to the top layer, like strokes.
        void Add(std::vector<XmlElementPtr> const& images);
        /// Removes pictures; returns where they were for Restore.
        std::vector<ImagePlace> Remove(std::vector<XmlElementPtr> const& images);
        /// Puts removed pictures back at their place.
        void Restore(std::vector<ImagePlace> const& places);

        /// Moves/scales pictures in place (their layer and position stay).
        void Transform(std::vector<XmlElementPtr> const& images, Matrix const& m);
        std::vector<ImageBounds> Capture(std::vector<XmlElementPtr> const& images) const;
        void Restore(std::vector<ImageBounds> const& bounds);

        /// Pictures were added or removed (put back by undo): those pictures.
        std::function<void(std::vector<XmlElementPtr> const&)> changed;
        /// Pictures got new bounds (moved, scaled).
        std::function<void(std::vector<XmlElementPtr> const&)> moved;

    private:
        void RaiseChanged(std::vector<XmlElementPtr> const& images) const;

        XmlElement& page;
    };
}
