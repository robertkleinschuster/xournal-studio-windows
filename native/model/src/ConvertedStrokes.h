#pragma once

// The elements of strokes drawn in this session (they have no element from the file): each is converted once and reused
// by every later snapshot (saving, the recovery copy every few seconds, export) while it is written with the same
// transform. A stroke's samples and look never change; its transform does (moved, scaled, undone) - then it is
// converted again.

#include "xournal/model/Ink.h"
#include "xournal/model/Xml.h"

#include <set>
#include <unordered_map>

namespace xournal
{
    class ConvertedStrokes
    {
    public:
        /// The stroke's element written with `transform`, as a new copy the caller may keep.
        XmlElementPtr ElementOf(StrokeId id, InkView const& ink, Matrix const& transform);

        /// A copy of a stroke took its place (same samples and look): its element is the copy's.
        void Transfer(StrokeId oldId, StrokeId newId);

        /// Forgets the strokes no longer on the page (erased, deleted, replaced by an undo's copy).
        void KeepOnly(std::set<StrokeId> const& live);

        /// How many strokes were converted so far (tests: each once per transform).
        int Conversions() const { return conversions; }

    private:
        struct Converted
        {
            Matrix transform;
            XmlElementPtr element;
        };

        std::unordered_map<StrokeId, Converted> elements;
        int conversions = 0;
    };
}
