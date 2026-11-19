#pragma once

// The file's pictures (<image>, <teximage>) and texts (<text>, <link>) as InkPages' PictureHost: selected, moved, scaled,
// deleted, duplicated and copied with the strokes, undone by the library. Moving and scaling write the element's
// attributes in place (XoppImage::SetBounds: left/top/right/bottom, or its matrix; XoppText::Transform: its corner and
// size), as the C# app did; an element moved onto another page goes to that page's top layer. While a selection is
// dragged the underlay draws them with the preview transform. The Text tool (FileTexts) shares the ids and marks the
// text it edits (the underlay leaves it to the library meanwhile).
// UI thread; the document's lock is taken around every change (the ink thread writes strokes into the same layers).

#include "TextDrawing.h"

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

        /// The text the Text tool edits (nothing: none); the underlay does not draw it.
        void SetEditing(XmlElement const* text) { editing = text; }
        bool IsEditing(XmlElement const* element) const { return element && element == editing; }

        /// The id of an element of the file (the same one each time), the element of an id.
        inkpages::PictureId IdOf(XmlElementPtr const& element);
        XmlElementPtr const& ElementOf(inkpages::PictureId id) const { return pictures.at(id); }
        /// The page holding an element (-1: on no page). Call with the document's lock held.
        int PageOf(XmlElement const* element) const;
        /// The topmost text under a document point (with the document's lock held by the caller).
        std::optional<inkpages::PictureId> TextAtLocked(inkpages::PointF point);
        /// The page's top left in document units.
        Point Origin(int page) const;

        /// Copies of pictures and texts in document units moved so that `corner` is at 0,0 (the clipboard's clip).
        std::vector<XmlElementPtr> CopiesOf(std::vector<inkpages::PictureId> const& pictures, Point corner);
        /// Adds pictures and texts given in document units (pasted) to the pages under their centers (top layer) and
        /// returns their ids.
        std::vector<inkpages::PictureId> Add(std::vector<XmlElementPtr> const& elements);

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
        static bool IsObject(XmlElement const& e);                 // a picture or a text
        std::vector<XmlElementPtr> ObjectsOf(int page) const;     // bottom to top
        Rect BoundsOf(XmlElement const& element) const;           // page units
        static void TransformInPlace(XmlElement& element, Matrix const& m);
        std::optional<inkpages::PictureId> TopmostAt(inkpages::PointF point, bool textsOnly);
        Matrix ToPage(inkpages::Affine const& m, int page) const; // a document transform as one in the page's units
        int PageAtPoint(Point document) const;

        std::shared_ptr<XoppDocument> doc;
        inkpages::WritingSurface& surface;
        std::mutex& documentLock;
        std::function<void(int)> invalidate;
        TextDrawing measure;
        XmlElement const* editing = nullptr;
        std::map<inkpages::PictureId, XmlElementPtr> pictures;
        std::map<XmlElement const*, inkpages::PictureId> idByElement;
        inkpages::PictureId nextId = 1;
        std::map<XmlElement const*, Matrix> previews;
        std::map<uint64_t, std::vector<std::pair<int, std::vector<ImagePlace>>>> removed;
        uint64_t nextToken = 1;
    };
}
