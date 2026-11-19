#pragma once

// The file's pictures (<image>, <teximage>) as InkPages' PictureHost: selected, moved, scaled, deleted, duplicated and
// copied with the strokes, undone by the library. Moving and scaling write the picture's attributes in place
// (XoppImage::SetBounds: left/top/right/bottom, or its matrix), as the C# app did; a picture moved onto another page goes
// to that page's top layer. While a selection is dragged the underlay draws the pictures with the preview transform.
// UI thread; the document's lock is taken around every change (the ink thread writes strokes into the same layers).

#include "xournal/model/XoppDocument.h"

#include "inkpages/WritingSurface.h"
#include "inkpages/model/PageMap.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace xournal::app
{
    class FilePictures : public inkpages::PictureHost
    {
    public:
        FilePictures(std::shared_ptr<XoppDocument> doc, inkpages::WritingSurface& surface, std::mutex& documentLock,
            std::function<void(int page)> invalidate);

        /// Pages were inserted, removed or moved: pictures removed from a page that is gone cannot come back there.
        void Remap(inkpages::PageMap const& map);

        /// The transform a drag shows a picture with (page units of its page), if it is being dragged.
        std::optional<Matrix> PreviewOf(XmlElement const* picture) const;

        std::vector<inkpages::PictureId> PicturesIn(int page, std::vector<inkpages::PointF> const& polygon) override;
        std::vector<inkpages::PictureId> PicturesOn(int page) override;
        inkpages::RectF Bounds(inkpages::PictureId picture) override;
        std::optional<inkpages::PictureId> PictureAt(inkpages::PointF point) override;
        void Preview(std::vector<inkpages::PictureId> const& pictures, inkpages::Affine const& m) override;
        void Transform(std::vector<inkpages::PictureId> const& pictures, inkpages::Affine const& m) override;
        uint64_t Remove(std::vector<inkpages::PictureId> const& pictures) override;
        void Restore(uint64_t token) override;
        std::vector<inkpages::PictureId> Duplicate(std::vector<inkpages::PictureId> const& pictures, inkpages::Affine const& m) override;

    private:
        inkpages::PictureId IdOf(XmlElementPtr const& picture);
        int PageOf(XmlElement const* picture) const;           // -1: on no page (removed)
        Point Origin(int page) const;                            // the page's top left in document units
        Matrix ToPage(inkpages::Affine const& m, int page) const; // a document transform as one in the page's units
        int PageAtPoint(Point document) const;

        std::shared_ptr<XoppDocument> doc;
        inkpages::WritingSurface& surface;
        std::mutex& documentLock;
        std::function<void(int)> invalidate;
        std::map<inkpages::PictureId, XmlElementPtr> pictures;
        std::map<XmlElement const*, inkpages::PictureId> idByElement;
        inkpages::PictureId nextId = 1;
        std::map<XmlElement const*, Matrix> previews;
        std::map<uint64_t, std::vector<std::pair<int, std::vector<ImagePlace>>>> removed;
        uint64_t nextToken = 1;
    };
}
