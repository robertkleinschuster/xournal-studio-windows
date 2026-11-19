// XoppPage::ContentBottom: where pasted content goes (below everything on the page). Its own file because it gathers
// every kind of content and estimates texts.

#include "xournal/model/XoppPage.h"

#include "xournal/model/Strokes.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppText.h"

#include <algorithm>
#include <cmath>

namespace xournal
{
    namespace
    {
        // The ink's lowest edge: its samples where the ink shows them, plus half the drawn width there.
        double InkBottom(StrokeInk const& ink, Matrix const& transform)
        {
            double bottom = 0;
            for (auto const& sample : ink.samples)
            {
                double drawn = ink.look.ignorePressure ? ink.look.size : ink.look.size * InkWidth::Factor(sample.pressure);
                bottom = std::max(bottom, transform.Apply(sample.position).y + drawn / 2);
            }
            return bottom;
        }

        // The laid-out box is the view's; estimated here (a line per paragraph at 1.2 x the size, about 0.6 x the size per
        // character, or the wrap width) - close enough to place pasted content below it. All four corners: a matrix may
        // turn or mirror the box.
        double TextBottom(XoppText const& text)
        {
            double charWidth = text.size * 0.6;
            std::vector<size_t> lengths;
            for (auto const& paragraph : text.Paragraphs())
            {
                lengths.push_back(XoppText::CharacterCount(paragraph));
            }
            double longest = static_cast<double>(*std::max_element(lengths.begin(), lengths.end()));
            double w = text.wrap ? *text.wrap : longest * charWidth;
            double lines = 0;
            if (text.wrap)
            {
                // A wrapped paragraph takes about (its estimated width / the wrap width) lines, at most one per character.
                for (size_t n : lengths)
                {
                    double count = static_cast<double>(n);
                    lines += std::clamp(std::ceil(count * charWidth / *text.wrap), 1.0, std::max(1.0, count));
                }
            }
            else
            {
                lines = static_cast<double>(lengths.size());
            }
            double h = lines * text.size * 1.2;
            double bottom = 0;
            for (Point corner : { Point{ 0, 0 }, Point{ w, 0 }, Point{ 0, h }, Point{ w, h } })
            {
                bottom = std::max(bottom, text.transform.Apply(corner).y);
            }
            return bottom;
        }
    }

    double XoppPage::ContentBottom() const
    {
        double bottom = 0;
        if (auto view = ink.View())
        {
            for (StrokeId id : view->Strokes())
            {
                bottom = std::max(bottom, InkBottom(view->InkOf(id), view->TransformOf(id)));
            }
        }
        else
        {
            for (auto const& record : ink.ReadStrokes())
            {
                bottom = std::max(bottom, InkBottom(record.ink, {}));
            }
        }
        for (auto const& image : images.List())
        {
            bottom = std::max(bottom, XoppImage::GetBounds(*image).Bottom());
        }
        for (auto const& stroke : DrawnStrokes())
        {
            auto shape = Strokes::ReadShape(*stroke);
            if (auto lowest = shape ? shape->Bottom() : std::nullopt) // only what is drawn
            {
                bottom = std::max(bottom, *lowest);
            }
        }
        for (auto const& text : Texts())
        {
            bottom = std::max(bottom, TextBottom(XoppText::Read(*text)));
        }
        return bottom;
    }
}
