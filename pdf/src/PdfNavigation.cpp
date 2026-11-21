#include "xournal/pdf/PdfNavigation.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/pdf/PageFrame.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include <format>
#include <map>
#include <typeinfo>

namespace xournal::pdf
{
    namespace
    {
        constexpr int Hidden = 1 << 1, NoView = 1 << 5;

        std::optional<PdfBox> Box(QPDFObjectHandle o)
        {
            if (!o.isRectangle()) return std::nullopt;
            auto r = o.getArrayAsRectangle();
            return PdfBox{ r.llx, r.lly, r.urx, r.ury };
        }

        std::optional<double> Number(QPDFObjectHandle array, int index)
        {
            if (index >= array.getArrayNItems()) return std::nullopt;
            auto item = array.getArrayItem(index);
            if (!item.isNumber()) return std::nullopt; // null: "unchanged"
            return item.getNumericValue();
        }

        template <class Body>
        auto Guarded(char const* what, Body body)
        {
            try
            {
                return body();
            }
            catch (Error const&)
            {
                throw;
            }
            catch (std::exception const& e)
            {
                // qpdf reports damaged PDFs with its own exceptions.
                std::string message = std::format("{}: {}: {}", what, typeid(e).name(), e.what());
                LogError(message);
                throw Error(message, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
            }
        }
    }

    struct PdfNavigation::Impl
    {
        std::vector<std::uint8_t> data; // qpdf reads from it while the object lives
        QPDF pdf;
        std::vector<QPDFPageObjectHelper> pages;
        std::vector<PageFrame> frames;
        std::map<QPDFObjGen, int> pageIndex;

        int PageOf(QPDFObjectHandle page) const
        {
            if (page.isInteger())
            {
                // Some writers put a page number into a local destination (a remote form); taken when it fits.
                auto n = page.getIntValue();
                return n >= 0 && n < static_cast<long long>(pages.size()) ? static_cast<int>(n) : -1;
            }
            auto it = pageIndex.find(page.getObjGen());
            return it == pageIndex.end() ? -1 : it->second;
        }

        // A resolved explicit destination: [page /XYZ left top zoom], [page /FitH top], ...
        std::optional<Destination> FromArray(QPDFObjectHandle dest) const
        {
            if (!dest.isArray() || dest.getArrayNItems() < 1) return std::nullopt;
            int page = PageOf(dest.getArrayItem(0));
            if (page < 0) return std::nullopt;
            Destination d{ page, std::nullopt };
            auto kind = dest.getArrayNItems() > 1 && dest.getArrayItem(1).isName() ? dest.getArrayItem(1).getName() : std::string();
            std::optional<double> left, top;
            if (kind == "/XYZ") { left = Number(dest, 2); top = Number(dest, 3); }
            else if (kind == "/FitH" || kind == "/FitBH") top = Number(dest, 2);
            else if (kind == "/FitV" || kind == "/FitBV") left = Number(dest, 2);
            else if (kind == "/FitR") { left = Number(dest, 2); top = Number(dest, 5); }
            if (left || top)
            {
                auto const& f = frames[static_cast<size_t>(page)];
                d.position = f.FromUserSpace(left.value_or(f.x1), top.value_or(f.y1 + f.height));
            }
            return d;
        }

        // A destination as written: explicit, or a name (/Dests) or string (/Names tree) to resolve.
        std::optional<Destination> Resolve(QPDFObjectHandle dest) const
        {
            if (dest.isName() || dest.isString())
            {
                dest = QPDFOutlineDocumentHelper::get(const_cast<QPDF&>(pdf)).resolveNamedDest(dest);
            }
            return FromArray(dest);
        }

        OutlineEntry Entry(QPDFOutlineObjectHelper& item, bool& keep) const
        {
            OutlineEntry entry;
            entry.title = FromUtf8(item.getTitle());
            auto handle = item.getObjectHandle();
            bool hasTarget = handle.hasKey("/Dest") || handle.hasKey("/A");
            if (hasTarget)
            {
                auto dest = item.getDest(); // explicit, named or a GoTo action's
                if (!dest.isNull()) entry.destination = FromArray(dest);
                if (!entry.destination)
                {
                    auto action = handle.getKey("/A");
                    bool goTo = handle.hasKey("/Dest") || (action.isDictionary() && action.getKey("/S").isNameAndEquals("/GoTo"));
                    Log(std::format("pdf outline: entry '{}': {}", item.getTitle(),
                                    goTo ? "its destination cannot be resolved" : "its action is not a GoTo"));
                }
            }
            for (auto& kid : item.getKids())
            {
                bool keepKid = true;
                auto child = Entry(kid, keepKid);
                if (keepKid) entry.children.push_back(std::move(child));
            }
            keep = !hasTarget || entry.destination || !entry.children.empty();
            return entry;
        }
    };

    PdfNavigation::PdfNavigation(std::span<std::uint8_t const> pdf) : impl(std::make_unique<Impl>())
    {
        Guarded("pdf navigation", [&] {
            auto& m = *impl;
            m.data.assign(pdf.begin(), pdf.end());
            m.pdf.setSuppressWarnings(true); // collected below and logged
            m.pdf.processMemoryFile("PDF", reinterpret_cast<char const*>(m.data.data()), m.data.size());
            m.pages = QPDFPageDocumentHelper(m.pdf).getAllPages();
            for (size_t i = 0; i < m.pages.size(); ++i)
            {
                auto& page = m.pages[i];
                m.pageIndex.emplace(page.getObjectHandle().getObjGen(), static_cast<int>(i));
                auto media = Box(page.getAttribute("/MediaBox", false));
                auto rotate = page.getAttribute("/Rotate", false);
                m.frames.push_back(PageFrame::Of(Box(page.getAttribute("/CropBox", false)), media.value_or(PdfBox{ 0, 0, 612, 792 }),
                                                 rotate.isInteger() ? rotate.getIntValue() : 0));
            }
            for (auto const& w : m.pdf.getWarnings()) Log(std::format("pdf navigation: qpdf warning: {}", w.what()));
            return 0;
        });
    }

    PdfNavigation::~PdfNavigation() = default;

    int PdfNavigation::PageCount() const { return static_cast<int>(impl->pages.size()); }
    Size PdfNavigation::PageSize(int pageIndex) const { return impl->frames.at(pageIndex).Shown(); }

    std::vector<OutlineEntry> PdfNavigation::Outline() const
    {
        return Guarded("pdf outline", [&] {
            std::vector<OutlineEntry> outline;
            auto& helper = QPDFOutlineDocumentHelper::get(impl->pdf);
            for (auto& item : helper.getTopLevelOutlines())
            {
                bool keep = true;
                auto entry = impl->Entry(item, keep);
                if (keep) outline.push_back(std::move(entry));
            }
            return outline;
        });
    }

    std::vector<Link> PdfNavigation::Links(int pageIndex) const
    {
        if (pageIndex < 0 || pageIndex >= PageCount())
        {
            ThrowInvalidData(std::format("pdf links: page {} of {}", pageIndex + 1, PageCount()));
        }
        return Guarded("pdf links", [&] {
            std::vector<Link> links;
            auto const& frame = impl->frames[static_cast<size_t>(pageIndex)];
            auto annotations = impl->pages[static_cast<size_t>(pageIndex)].getObjectHandle().getKey("/Annots");
            if (!annotations.isArray()) return links;
            for (auto& a : annotations.aitems())
            {
                if (!a.isDictionary() || !a.getKey("/Subtype").isNameAndEquals("/Link")) continue;
                auto flags = a.getKey("/F");
                if (flags.isInteger() && (flags.getIntValue() & (Hidden | NoView)) != 0) continue;
                auto rect = Box(a.getKey("/Rect"));
                if (!rect)
                {
                    Log(std::format("pdf links: page {}: a link without rectangle", pageIndex + 1));
                    continue;
                }
                Rect area = frame.Area(*rect);
                auto action = a.getKey("/A");
                std::string kind = action.isDictionary() && action.getKey("/S").isName() ? action.getKey("/S").getName() : std::string();
                if (a.hasKey("/Dest") || kind == "/GoTo")
                {
                    auto destination = impl->Resolve(a.hasKey("/Dest") ? a.getKey("/Dest") : action.getKey("/D"));
                    if (destination) links.push_back({ area, *destination });
                    else Log(std::format("pdf links: page {}: a link whose destination cannot be resolved", pageIndex + 1));
                }
                else if (kind == "/URI" && action.getKey("/URI").isString())
                {
                    // URIs are 7-bit ASCII (ISO 32000-1, 12.6.4.7); a UTF-8 or UTF-16 string is taken as text.
                    links.push_back({ area, FromUtf8(action.getKey("/URI").getUTF8Value()) });
                }
                else
                {
                    Log(std::format("pdf links: page {}: a link with action {} is not followed", pageIndex + 1,
                                    kind.empty() ? std::string("(none)") : kind));
                }
            }
            return links;
        });
    }
}
