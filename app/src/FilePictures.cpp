#include "FilePictures.h"

#include "Bitmaps.h"
#include "Direct2D.h"
#include "xournal/model/XoppFormat.h"
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

        std::optional<Size> RotationSize(XmlElement const& element, Matrix const& m)
        {
            if (!XoppImage::IsImage(element) || XoppFormat::ReadMatrix(element) ||
                (m.m12 == 0 && m.m21 == 0 && m.m11 >= 0 && m.m22 >= 0)) return {};
            if (auto size = XoppImage::NaturalSize(element)) return size;
            auto data = XoppImage::GetData(element);
            Size size = XoppImage::IsPdf(data) ? pdf::PdfNavigation(data).PageSize(0) : Bitmaps::NaturalSize(data);
            if (!(size.width > 0 && size.height > 0)) ThrowInvalidData("picture of size 0");
            return size;
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

    std::pair<Rect, Matrix> FilePictures::LocalPlacement(XmlElement const& element) const
    {
        if (XoppImage::IsImage(element))
        {
            auto placement = XoppImage::GetPlacement(element);
            return { { 0, 0, placement.size.width, placement.size.height }, placement.transform };
        }
        auto text = XoppText::Read(element);
        return { measure.LocalBounds(text), text.transform };
    }

    void FilePictures::TransformInPlace(XmlElement& element, Matrix const& m, std::optional<Size> intrinsic)
    {
        if (XoppImage::IsImage(element)) XoppImage::Transform(element, m, intrinsic);
        else XoppText::Transform(element, m);
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
        auto objects = ObjectsOf(page);
        if (objects.empty()) return found;
        if (!geometryFactory)
            Check("D2D1CreateFactory (selection)", D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, geometryFactory.GetAddressOf()));
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> lasso;
        Check("CreatePathGeometry (selection)", geometryFactory->CreatePathGeometry(&lasso));
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        Check("ID2D1PathGeometry::Open (selection)", lasso->Open(&sink));
        sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
        sink->BeginFigure({ polygon.front().x, polygon.front().y }, D2D1_FIGURE_BEGIN_FILLED);
        for (size_t i = 1; i < polygon.size(); ++i) sink->AddLine({ polygon[i].x, polygon[i].y });
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        Check("ID2D1GeometrySink::Close (selection)", sink->Close());

        Point o = Origin(page);
        for (auto const& object : objects)
        {
            auto [b, placement] = LocalPlacement(*object);
            Microsoft::WRL::ComPtr<ID2D1RectangleGeometry> rectangle;
            Check("CreateRectangleGeometry (selection)", geometryFactory->CreateRectangleGeometry(
                D2D1::RectF(static_cast<float>(b.Left()), static_cast<float>(b.Top()),
                    static_cast<float>(b.Right()), static_cast<float>(b.Bottom())), &rectangle));
            auto transform = ToD2D(placement * Matrix::Translation(o.x, o.y));
            D2D1_GEOMETRY_RELATION relation{};
            Check("CompareWithGeometry (selection)", lasso->CompareWithGeometry(rectangle.Get(), &transform, &relation));
            if (relation == D2D1_GEOMETRY_RELATION_CONTAINS) found.push_back(IdOf(object));
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
            auto [b, placement] = LocalPlacement(**it);
            auto inverse = ToD2D(placement);
            if (!D2D1InvertMatrix(&inverse)) continue;
            Point local = Matrix{ inverse._11, inverse._12, inverse._21, inverse._22, inverse._31, inverse._32 }
                .Apply({ point.x - o.x, point.y - o.y });
            if (local.x >= b.Left() && local.x <= b.Right() && local.y >= b.Top() && local.y <= b.Bottom())
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

    bool FilePictures::DrawLifted(ID2D1DeviceContext* dc, std::vector<inkpages::PictureId> const& ids, float pixelsPerUnit)
    {
        if (!drawObject) return false;
        std::set<XmlElement const*> wanted;
        for (auto id : ids) wanted.insert(pictures.at(id).get());
        D2D1_MATRIX_3X2_F documentTransform;
        dc->GetTransform(&documentTransform);
        std::scoped_lock guard(documentLock);
        for (int page = 0; page < static_cast<int>(doc->Pages().size()); ++page)
        {
            Point o = Origin(page);
            dc->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(o.x), static_cast<float>(o.y)) * documentTransform);
            for (auto const& object : ObjectsOf(page))
            {
                if (wanted.contains(object.get())) drawObject(page, object, dc, pixelsPerUnit); // bottom to top
            }
        }
        dc->SetTransform(documentTransform);
        return true;
    }

    void FilePictures::Lift(std::vector<inkpages::PictureId> const& ids, bool lift)
    {
        std::set<int> pages;
        {
            std::scoped_lock guard(documentLock);
            for (auto id : ids)
            {
                auto const& element = pictures.at(id);
                int page = PageOf(element.get());
                if (lift) lifted.insert(element.get());
                else lifted.erase(element.get());
                if (page >= 0) pages.insert(page);
            }
        }
        for (int page : pages) invalidate(page);
    }

    void FilePictures::Transform(std::vector<inkpages::PictureId> const& ids, inkpages::Affine const& m)
    {
        struct Change
        {
            XmlElementPtr element;
            int source, target;
            Matrix transform;
            std::optional<Size> intrinsic;
        };
        std::set<int> pages;
        {
            std::scoped_lock guard(documentLock);
            std::vector<Change> changes;
            changes.reserve(ids.size());
            for (auto id : ids)
            {
                auto const& element = pictures.at(id);
                int page = PageOf(element.get());
                if (page < 0)
                {
                    LogError(std::format("pictures: transform of picture {} that is on no page", id));
                    continue;
                }
                Matrix onPage = ToPage(m, page);
                auto [b, placement] = LocalPlacement(*element);
                Point center = (placement * onPage).Apply({ b.x + b.width / 2, b.y + b.height / 2 });
                Point o = Origin(page);
                int target = PageAtPoint({ center.x + o.x, center.y + o.y });
                if (target < 0) target = page;
                Point t = Origin(target);
                changes.push_back({ element, page, target, onPage * Matrix::Translation(o.x - t.x, o.y - t.y),
                    RotationSize(*element, onPage) });
                pages.insert(page);
                pages.insert(target);
            }
            for (auto const& change : changes)
            {
                auto const& element = change.element;
                previews.erase(element.get());
                TransformInPlace(*element, change.transform, change.intrinsic);
                if (change.source != change.target)
                {
                    doc->Pages()[change.source]->Images().Remove({ element });
                    doc->Pages()[change.target]->Images().Add({ element });
                }
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
