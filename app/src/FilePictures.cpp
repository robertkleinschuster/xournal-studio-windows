#include "FilePictures.h"

#include "Bitmaps.h"
#include "xournal/base/Error.h"
#include "xournal/pdf/PdfNavigation.h"

#include "xournal/base/Log.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppText.h"

#include <format>
#include <set>

namespace xournal::app
{
    namespace
    {
        Matrix MatrixOf(inkpages::Affine const& a) { return { a.m11, a.m12, a.m21, a.m22, a.dx, a.dy }; }

        // Even-odd point in polygon.
        bool Inside(std::vector<inkpages::PointF> const& polygon, Point p)
        {
            bool inside = false;
            for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
            {
                auto const& a = polygon[i];
                auto const& b = polygon[j];
                if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
                {
                    inside = !inside;
                }
            }
            return inside;
        }
    }

    FilePictures::FilePictures(std::shared_ptr<XoppDocument> doc, inkpages::WritingSurface& surface, std::mutex& documentLock,
        std::function<void(int)> invalidate)
        : doc(std::move(doc)), surface(surface), documentLock(documentLock), invalidate(std::move(invalidate))
    {
    }

    inkpages::PictureId FilePictures::IdOf(XmlElementPtr const& element)
    {
        auto [it, added] = idByElement.try_emplace(element.get(), nextId);
        if (added)
        {
            pictures[nextId++] = element;
        }
        return it->second;
    }

    bool FilePictures::IsObject(XmlElement const& e) { return XoppImage::IsImage(e) || XoppText::IsText(e); }

    std::vector<XmlElementPtr> FilePictures::ObjectsOf(int page) const
    {
        std::vector<XmlElementPtr> objects;
        for (auto const& layer : doc->Pages()[page]->Layers())
        {
            for (auto const& e : layer->Elements())
            {
                if (IsObject(*e)) objects.push_back(e);
            }
        }
        return objects;
    }

    Rect FilePictures::BoundsOf(XmlElement const& element) const
    {
        return XoppImage::IsImage(element) ? XoppImage::GetBounds(element) : measure.Bounds(XoppText::Read(element));
    }

    void FilePictures::TransformInPlace(XmlElement& element, Matrix const& m)
    {
        if (XoppImage::IsImage(element))
        {
            std::optional<Size> intrinsic;
            if ((m.m12 != 0 || m.m21 != 0 || m.m11 < 0 || m.m22 < 0) && !element.Attribute(L"matrix"))
            {
                intrinsic = XoppImage::NaturalSize(element);
                if (!intrinsic)
                {
                    auto data = XoppImage::GetData(element);
                    intrinsic = XoppImage::IsPdf(data) ? pdf::PdfNavigation(data).PageSize(0) : Bitmaps::NaturalSize(data);
                    if (!(intrinsic->width > 0 && intrinsic->height > 0)) ThrowInvalidData("picture of size 0");
                }
            }
            XoppImage::Transform(element, m, intrinsic);
        }
        else
        {
            XoppText::Transform(element, m);
        }
    }

    int FilePictures::PageOf(XmlElement const* element) const
    {
        auto const& pages = doc->Pages();
        for (int i = 0; i < static_cast<int>(pages.size()); ++i)
        {
            for (auto const& object : ObjectsOf(i))
            {
                if (object.get() == element) return i;
            }
        }
        return -1;
    }

    Point FilePictures::Origin(int page) const
    {
        auto r = surface.Layout().Page(page);
        return { r.left, r.top };
    }

    Matrix FilePictures::ToPage(inkpages::Affine const& m, int page) const
    {
        Point o = Origin(page);
        return Matrix::Translation(o.x, o.y) * MatrixOf(m) * Matrix::Translation(-o.x, -o.y);
    }

    int FilePictures::PageAtPoint(Point document) const
    {
        auto page = surface.Layout().NearestPage({ static_cast<float>(document.x), static_cast<float>(document.y) });
        return page ? *page : -1;
    }

    void FilePictures::Remap(inkpages::PageMap const& map)
    {
        for (auto& [token, places] : removed)
        {
            std::erase_if(places, [&](auto const& place) { return map.Removes(place.first); });
            for (auto& place : places) place.first = map(place.first);
        }
    }

    std::optional<Matrix> FilePictures::PreviewOf(XmlElement const* picture) const
    {
        auto it = previews.find(picture);
        return it == previews.end() ? std::nullopt : std::optional<Matrix>(it->second);
    }

    std::vector<inkpages::PictureId> FilePictures::PicturesIn(int page, std::vector<inkpages::PointF> const& polygon)
    {
        std::scoped_lock guard(documentLock);
        std::vector<inkpages::PictureId> found;
        if (page < 0 || page >= static_cast<int>(doc->Pages().size()) || polygon.size() < 3) return found;
        Point o = Origin(page);
        for (auto const& image : ObjectsOf(page))
        {
            Rect b = BoundsOf(*image);
            Point corners[] = { { b.Left(), b.Top() }, { b.Right(), b.Top() }, { b.Right(), b.Bottom() }, { b.Left(), b.Bottom() } };
            bool all = true;
            for (auto c : corners) all = all && Inside(polygon, { c.x + o.x, c.y + o.y });
            if (all) found.push_back(IdOf(image));
        }
        return found;
    }

    std::vector<inkpages::PictureId> FilePictures::PicturesOn(int page)
    {
        std::scoped_lock guard(documentLock);
        std::vector<inkpages::PictureId> found;
        if (page < 0 || page >= static_cast<int>(doc->Pages().size())) return found;
        for (auto const& object : ObjectsOf(page)) found.push_back(IdOf(object));
        return found;
    }

    inkpages::RectF FilePictures::Bounds(inkpages::PictureId picture)
    {
        std::scoped_lock guard(documentLock);
        auto const& element = pictures.at(picture);
        int page = PageOf(element.get());
        if (page < 0) return {};
        Rect b = BoundsOf(*element);
        Point o = Origin(page);
        return { static_cast<float>(b.Left() + o.x), static_cast<float>(b.Top() + o.y), static_cast<float>(b.Right() + o.x),
            static_cast<float>(b.Bottom() + o.y) };
    }

    std::optional<inkpages::PictureId> FilePictures::PictureAt(inkpages::PointF point)
    {
        std::scoped_lock guard(documentLock);
        return TopmostAt(point, false);
    }

    std::optional<inkpages::PictureId> FilePictures::TextAtLocked(inkpages::PointF point)
    {
        return TopmostAt(point, true);
    }

    std::optional<inkpages::PictureId> FilePictures::TopmostAt(inkpages::PointF point, bool textsOnly)
    {
        int page = PageAtPoint({ point.x, point.y });
        if (page < 0) return std::nullopt;
        Point o = Origin(page);
        auto objects = ObjectsOf(page);
        for (auto it = objects.rbegin(); it != objects.rend(); ++it) // the topmost first
        {
            if (textsOnly && !XoppText::IsText(**it)) continue;
            Rect b = BoundsOf(**it);
            if (point.x >= b.Left() + o.x && point.x <= b.Right() + o.x && point.y >= b.Top() + o.y && point.y <= b.Bottom() + o.y)
            {
                return IdOf(*it);
            }
        }
        return std::nullopt;
    }

    void FilePictures::Preview(std::vector<inkpages::PictureId> const& ids, inkpages::Affine const& m)
    {
        std::set<int> pages;
        {
            std::scoped_lock guard(documentLock);
            for (auto id : ids)
            {
                auto const& element = pictures.at(id);
                int page = PageOf(element.get());
                if (page < 0) continue;
                if (m.IsIdentity()) previews.erase(element.get());
                else previews[element.get()] = ToPage(m, page);
                pages.insert(page);
            }
        }
        for (int page : pages) invalidate(page);
    }

    void FilePictures::Transform(std::vector<inkpages::PictureId> const& ids, inkpages::Affine const& m)
    {
        std::set<int> pages;
        {
            std::scoped_lock guard(documentLock);
            for (auto id : ids)
            {
                auto const& element = pictures.at(id);
                previews.erase(element.get());
                int page = PageOf(element.get());
                if (page < 0)
                {
                    LogError(std::format("pictures: transform of picture {} that is on no page", id));
                    continue;
                }
                Matrix onPage = ToPage(m, page);
                Rect moved = XoppImage::Transform(BoundsOf(*element), onPage);
                Point o = Origin(page);
                int target = PageAtPoint({ moved.x + moved.width / 2 + o.x, moved.y + moved.height / 2 + o.y });
                pages.insert(page);
                if (target < 0 || target == page)
                {
                    TransformInPlace(*element, onPage);
                    continue;
                }
                // Onto another page: into that page's units, then into its top layer.
                Point t = Origin(target);
                TransformInPlace(*element, onPage * Matrix::Translation(o.x - t.x, o.y - t.y));
                doc->Pages()[page]->Images().Remove({ element }); // any element: its layer and place
                doc->Pages()[target]->Images().Add({ element });
                pages.insert(target);
            }
        }
        for (int page : pages) invalidate(page);
    }

    uint64_t FilePictures::Remove(std::vector<inkpages::PictureId> const& ids)
    {
        std::vector<std::pair<int, std::vector<ImagePlace>>> places;
        {
            std::scoped_lock guard(documentLock);
            for (auto id : ids)
            {
                auto const& element = pictures.at(id);
                int page = PageOf(element.get());
                if (page < 0) continue;
                places.push_back({ page, doc->Pages()[page]->Images().Remove({ element }) });
            }
        }
        for (auto const& [page, _] : places) invalidate(page);
        uint64_t token = nextToken++;
        removed[token] = std::move(places);
        return token;
    }

    void FilePictures::Restore(uint64_t token)
    {
        auto it = removed.find(token);
        if (it == removed.end())
        {
            LogError(std::format("pictures: restore of unknown token {}", token));
            return;
        }
        auto places = std::move(it->second);
        removed.erase(it);
        {
            std::scoped_lock guard(documentLock);
            for (auto const& [page, pagePlaces] : places)
            {
                doc->Pages()[page]->Images().Restore(pagePlaces);
            }
        }
        for (auto const& [page, _] : places) invalidate(page);
    }

    std::vector<XmlElementPtr> FilePictures::CopiesOf(std::vector<inkpages::PictureId> const& ids, Point corner)
    {
        std::vector<XmlElementPtr> copies;
        std::scoped_lock guard(documentLock);
        for (auto id : ids)
        {
            auto const& element = pictures.at(id);
            int page = PageOf(element.get());
            if (page < 0)
            {
                LogError(std::format("pictures: picture {} to copy is on no page - left out", id));
                continue;
            }
            Point o = Origin(page);
            auto copy = element->Clone();
            TransformInPlace(*copy, Matrix::Translation(o.x - corner.x, o.y - corner.y));
            copies.push_back(copy);
        }
        return copies;
    }

    std::vector<inkpages::PictureId> FilePictures::Add(std::vector<XmlElementPtr> const& elements)
    {
        std::vector<inkpages::PictureId> ids;
        std::set<int> pages;
        {
            std::scoped_lock guard(documentLock);
            for (auto const& element : elements)
            {
                Rect r = BoundsOf(*element);
                int page = PageAtPoint({ r.x + r.width / 2, r.y + r.height / 2 });
                if (page < 0)
                {
                    LogError("pictures: no page to add a pasted picture or text to - left out");
                    continue;
                }
                Point o = Origin(page);
                TransformInPlace(*element, Matrix::Translation(-o.x, -o.y));
                doc->Pages()[page]->Images().Add({ element }); // the top layer, like strokes
                ids.push_back(IdOf(element));
                pages.insert(page);
            }
        }
        for (int page : pages) invalidate(page);
        return ids;
    }

    std::vector<inkpages::PictureId> FilePictures::Duplicate(std::vector<inkpages::PictureId> const& ids, inkpages::Affine const& m)
    {
        std::vector<inkpages::PictureId> copies;
        std::set<int> pages;
        {
            std::scoped_lock guard(documentLock);
            for (auto id : ids)
            {
                auto const& element = pictures.at(id);
                int page = PageOf(element.get());
                if (page < 0) continue;
                auto copy = element->Clone();
                TransformInPlace(*copy, ToPage(m, page));
                doc->Pages()[page]->Images().Add({ copy });
                copies.push_back(IdOf(copy));
                pages.insert(page);
            }
        }
        for (int page : pages) invalidate(page);
        return copies;
    }
}
