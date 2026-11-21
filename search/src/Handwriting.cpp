// Handwriting recognition with Windows' ink analysis (Windows.UI.Input.Inking.Analysis.InkAnalyzer; works unpackaged),
// the model's strokes turned into InkStrokes with InkStrokeBuilder, and the cache of recognised pages.

#include "xournal/search/Search.h"
#include "Recognition.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/model/Strokes.h"
#include "xournal/base/Text.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Input.Inking.Analysis.h>
#include <winrt/Windows.UI.Input.Inking.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace xournal::search
{
    namespace
    {
        using namespace winrt::Windows::UI::Input::Inking;
        using namespace winrt::Windows::UI::Input::Inking::Analysis;

        std::vector<RecognizedWord> Analyze(std::vector<InkStroke> const& strokes)
        {
            std::vector<RecognizedWord> words;
            if (strokes.empty()) return words;

            InkAnalyzer analyzer;
            analyzer.AddDataForStrokes(strokes);
            auto result = analyzer.AnalyzeAsync().get();
            if (result.Status() != InkAnalysisStatus::Updated)
            {
                Log(std::format("search: ink analysis of a page ended with status {}", static_cast<int>(result.Status())));
            }
            // Lines top to bottom, their words in the analyzer's (reading) order.
            std::vector<IInkAnalysisNode> lines;
            for (auto const& node : analyzer.AnalysisRoot().FindNodes(InkAnalysisNodeKind::Line)) lines.push_back(node);
            std::stable_sort(lines.begin(), lines.end(), [](IInkAnalysisNode const& a, IInkAnalysisNode const& b) {
                return a.BoundingRect().Y < b.BoundingRect().Y;
            });
            int number = 0;
            for (auto const& line : lines)
            {
                for (auto const& child : line.Children())
                {
                    if (child.Kind() != InkAnalysisNodeKind::InkWord) continue;
                    auto word = child.as<InkAnalysisInkWord>();
                    auto r = word.BoundingRect();
                    words.push_back({ std::wstring(word.RecognizedText()), Rect{ r.X, r.Y, r.Width, r.Height }, number });
                }
                ++number;
            }
            return words;
        }

        void Hash(std::uint64_t& h, std::wstring_view s)
        {
            for (wchar_t c : s)
            {
                h = (h ^ static_cast<std::uint64_t>(c)) * 1099511628211ull;
            }
            h = (h ^ 0xFFFFull) * 1099511628211ull; // separator
        }

        std::vector<XmlElementPtr> StrokeElements(XmlElement const& page)
        {
            std::vector<XmlElementPtr> strokes;
            for (auto const& layer : page.Elements(L"layer"))
            {
                for (auto const& e : layer->Elements(L"stroke")) strokes.push_back(e);
            }
            return strokes;
        }
    }

    std::optional<std::vector<RecognizedWord>> HandwritingCache::Find(std::uint64_t strokes) const
    {
        std::lock_guard guard(lock);
        auto it = pages.find(strokes);
        if (it == pages.end()) return std::nullopt;
        return it->second;
    }

    void HandwritingCache::Store(std::uint64_t strokes, std::vector<RecognizedWord> words)
    {
        std::lock_guard guard(lock);
        pages[strokes] = std::move(words);
    }

    size_t HandwritingCache::Size() const
    {
        std::lock_guard guard(lock);
        return pages.size();
    }

    void HandwritingCache::Clear()
    {
        std::lock_guard guard(lock);
        pages.clear();
    }

    std::uint64_t StrokesKey(XmlElement const& page)
    {
        // What decides the recognition: every stroke's tool and points, in order (colors and widths do not).
        std::uint64_t h = 14695981039346656037ull;
        for (auto const& e : StrokeElements(page))
        {
            Hash(h, e->AttributeOr(L"tool", L"pen"));
            Hash(h, e->Value());
        }
        return h;
    }

    std::vector<RecognizedWord> RecognizeHandwriting(XmlElement const& page)
    {
        try
        {
            InkStrokeBuilder builder;
            std::vector<InkStroke> strokes;
            for (auto const& e : StrokeElements(page))
            {
                auto ink = Strokes::FromXml(*e);
                if (!ink || ink->look.highlighter || ink->samples.empty()) continue; // highlighters mark, they do not write
                std::vector<InkPoint> points;
                points.reserve(ink->samples.size());
                for (auto const& s : ink->samples)
                {
                    points.emplace_back(winrt::Windows::Foundation::Point{ static_cast<float>(s.position.x), static_cast<float>(s.position.y) },
                                        std::clamp(s.pressure, 0.0f, 1.0f));
                }
                strokes.push_back(builder.CreateStrokeFromInkPoints(points, winrt::Windows::Foundation::Numerics::float3x2::identity()));
            }
            return Analyze(strokes);
        }
        catch (winrt::hresult_error const& e)
        {
            ThrowHr(std::format("search: handwriting recognition ({})", ToUtf8(std::wstring(e.message()))), e.code());
        }
    }

    std::vector<RecognizedWord> detail::RecognizePenPaths(std::vector<std::vector<Point>> const& paths)
    {
        InkStrokeBuilder builder;
        std::vector<InkStroke> strokes;
        strokes.reserve(paths.size());
        for (auto const& path : paths)
        {
            std::vector<winrt::Windows::Foundation::Point> points;
            points.reserve(path.size());
            for (auto p : path) points.push_back({ static_cast<float>(p.x), static_cast<float>(p.y) });
            strokes.push_back(builder.CreateStroke(points));
        }
        return Analyze(strokes);
    }
}
