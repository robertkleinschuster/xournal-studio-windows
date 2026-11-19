#include "FilePictures.h"

#include "xournal/base/Log.h"
#include "xournal/model/XoppImage.h"

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

    inkpages::PictureId FilePictures::IdOf(XmlElementPtr const& picture)
    {
        auto [it, added] = idByElement.try_emplace(picture.get(), nextId);
        if (added)
        {
            pictures[nextId++] = picture;
        }
        return it->second;
    }

    int FilePictures::PageOf(XmlElement const* picture) const
    {
        auto const& pages = doc->Pages();
        for (int i = 0; i < static_cast<int>(pages.size()); ++i)
        {
            for (auto const& image : pages[i]->Images().List())
            {
                if (image.get() == picture) return i;
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
        for (auto const& image : doc->Pages()[page]->Images().List())
        {
            Rect b = XoppImage::GetBounds(*image);
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
        for (auto const& image : doc->Pages()[page]->Images().List()) found.push_back(IdOf(image));
        return found;
    }

    inkpages::RectF FilePictures::Bounds(inkpages::PictureId picture)
    {
        std::scoped_lock guard(documentLock);
        auto const& element = pictures.at(picture);
        int page = PageOf(element.get());
        if (page < 0) return {};
        Rect b = XoppImage::GetBounds(*element);
        Point o = Origin(page);
        return { static_cast<float>(b.Left() + o.x), static_cast<float>(b.Top() + o.y), static_cast<float>(b.Right() + o.x),
            static_cast<float>(b.Bottom() + o.y) };
    }

    std::optional<inkpages::PictureId> FilePictures::PictureAt(inkpages::PointF point)
    {
        std::scoped_lock guard(documentLock);
        int page = PageAtPoint({ point.x, point.y });
        if (page < 0) return std::nullopt;
        Point o = Origin(page);
        auto images = doc->Pages()[page]->Images().List();
        for (auto it = images.rbegin(); it != images.rend(); ++it) // the topmost first
        {
            Rect b = XoppImage::GetBounds(**it);
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
                Rect moved = XoppImage::Transform(XoppImage::GetBounds(*element), ToPage(m, page));
                Point o = Origin(page);
                int target = PageAtPoint({ moved.x + moved.width / 2 + o.x, moved.y + moved.height / 2 + o.y });
                pages.insert(page);
                if (target < 0 || target == page)
                {
                    doc->Pages()[page]->Images().Transform({ element }, ToPage(m, page));
                    continue;
                }
                // Onto another page: its bounds in that page's units, then into that page's top layer.
                Point t = Origin(target);
                doc->Pages()[page]->Images().Remove({ element });
                XoppImage::SetBounds(*element, { moved.x + o.x - t.x, moved.y + o.y - t.y, moved.width, moved.height });
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
                XoppImage::SetBounds(*copy, XoppImage::Transform(XoppImage::GetBounds(*copy), ToPage(m, page)));
                doc->Pages()[page]->Images().Add({ copy });
                copies.push_back(IdOf(copy));
                pages.insert(page);
            }
        }
        for (int page : pages) invalidate(page);
        return copies;
    }
}
