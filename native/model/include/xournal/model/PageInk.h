#pragma once

// The binding between a page's stroke elements and the ink layer. Without a view the page is only XML; a view gets the
// strokes that can be ink (ReadStrokes, on any thread), the ink layer adds them and tells the page which ink stroke is
// which element (Attach). From then on the ink is the page's strokes: unchanged strokes are written back as their
// original elements (so attributes the app ignores survive a save), strokes drawn in this session are converted, erased
// ones left out. When the view goes, the ink is written into the element (Detach) and the page holds no ink again.

#include "xournal/model/Geometry.h"
#include "xournal/model/Ink.h"
#include "xournal/model/Xml.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xournal
{
    class ConvertedStrokes;

    /// A stroke element that can be ink, with its ink.
    struct InkRecord
    {
        StrokeInk ink;
        XmlElementPtr element;
    };

    /// Where strokes were (each stroke's transform and its element's coordinates, if it has one), to put them back
    /// exactly (undo/redo of a move or scale, no rounding).
    struct StrokePlacement
    {
        StrokeId id = 0;
        Matrix transform;
        std::optional<std::wstring> coordinates;
    };

    class PageInk
    {
    public:
        /// `page`: the page's element (owned by its XoppPage, which owns this binding).
        explicit PageInk(XmlElement& page);
        ~PageInk();
        PageInk(PageInk const&) = delete;
        PageInk& operator=(PageInk const&) = delete;

        /// The page's strokes that can be ink, in drawing order, each with its element (strokes drawn from the file and
        /// strokes without points are not ink). Any thread while the page has no view: nothing changes the element then.
        std::vector<InkRecord> ReadStrokes() const;

        /// A view's ink layer added the strokes read: from now on `view` (borrowed until Detach) is the page's strokes.
        void Attach(InkView const& view, std::vector<std::pair<StrokeId, XmlElementPtr>> const& strokes);
        bool IsAttached() const { return view != nullptr; }
        InkView const* View() const { return view; }

        /// The view is released: the ink is written into the element (WriteBack) and the page keeps no ink.
        void Detach();

        /// A stroke made from an element was added to the ink (pasted, duplicated): written as that element, in the
        /// ink's drawing order. Make the ink with Strokes::InkOf (nothing: it cannot be ink).
        void Adopt(StrokeId id, XmlElementPtr element);

        /// Writes the ink into the page's element exactly as SnapshotOf writes it; the page then knows no stroke by id.
        void WriteBack(InkView const& ink);

        /// The page element as it is with this ink on it (a detached copy; reading only: the page keeps every element,
        /// so an undo can bring an erased stroke back with its original element).
        XmlElementPtr SnapshotOf(InkView const& ink);

        /// Undo/redo put a copy of a stroke back in place of the removed original: its elements go to the copy.
        void RenameStroke(StrokeId oldId, StrokeId newId);

        /// A move/scale is previewed on the stroke (the ink shows it): until EndPreview the page writes it with
        /// `committed`, so a snapshot taken at any moment never holds a half-done move.
        void BeginPreview(StrokeId id, Matrix const& committed);
        std::optional<Matrix> CommittedTransform(StrokeId id) const;
        void EndPreview(std::vector<StrokeId> const& ids);

        /// The ink already shows the stroke transformed: its element (if it has one) follows.
        void RecordTransform(StrokeId id, Matrix const& m);

        /// The stroke as a .xopp element (a copy): its original element (exact widths, unknown attributes) or converted.
        XmlElementPtr ElementOf(StrokeId id) const;

        std::vector<StrokePlacement> CapturePlacement(std::vector<StrokeId> const& strokes,
            std::function<Matrix(StrokeId)> const& transformOf = {}) const;
        /// Puts the elements' coordinates back; `current`: the placement's strokes as they are now (the copies undo/redo
        /// put in their place; nothing: gone). The ink layer sets the transforms (placement[i].transform) itself.
        void RestorePlacement(std::vector<StrokePlacement> const& placement, std::vector<std::optional<StrokeId>> const& current);

        /// Strokes drawn in this session converted so far (tests: each once per transform).
        int Conversions() const;

    private:
        Matrix WrittenTransform(InkView const& ink, StrokeId id) const;
        XmlElement& TopLayer() const;

        XmlElement& page;
        InkView const* view = nullptr;
        std::unordered_map<StrokeId, XmlElementPtr> strokeElements; // from the file (or already written), by ink id
        std::unordered_map<StrokeId, Matrix> previewed;
        std::unique_ptr<ConvertedStrokes> converted;
    };
}
