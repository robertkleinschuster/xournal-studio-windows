#include "PdfLinks.h"

#include "Dialogs.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/pdf/PdfPlaces.h"

#include <shellapi.h>

#include <format>

namespace xournal::app
{
    namespace
    {
        bool Contains(Rect const& r, Point p) { return p.x >= r.Left() && p.x <= r.Right() && p.y >= r.Top() && p.y <= r.Bottom(); }

        // Only addresses a browser or mail program opens: a PDF can name any URI, and ShellExecute would also start
        // programs and files ("file:", "ms-..." handlers).
        bool IsWebAddress(std::wstring const& uri)
        {
            for (wchar_t const* scheme : { L"http://", L"https://", L"mailto:" })
            {
                if (uri.size() > wcslen(scheme) && StartsWithIgnoreCase(uri, scheme)) return true;
            }
            return false;
        }
    }

    PdfLinks::PdfLinks(HWND owner, inkpages::WritingSurface& surface, DocumentView& view)
        : owner(owner), surface(surface), view(view)
    {
    }

    bool PdfLinks::Go(pdf::Destination const& destination)
    {
        auto doc = view.Document();
        auto background = view.Pdf();
        if (!doc) return false;
        auto shown = [background](int pdfPage) { return background ? background->ShownSize(pdfPage) : std::nullopt; };
        auto place = pdf::FindPlace(*doc, destination, surface.PageInView().value_or(0), shown);
        if (!place)
        {
            Log(std::format("pdf links: no page shows PDF page {}", destination.page + 1));
            return false;
        }
        Log(std::format("pdf links: to page {} at {:.0f},{:.0f} (PDF page {})", place->page + 1, place->point.x,
            place->point.y, destination.page + 1));
        surface.ScrollToPage(place->page); // the place on it: InkPages' ScrollTo(page, point) once it is there
        return true;
    }

    std::vector<pdf::Link> const& PdfLinks::LinksOf(pdf::PdfNavigation const& navigation, int pdfPage)
    {
        if (cachedFor != &navigation)
        {
            links.clear();
            cachedFor = &navigation;
        }
        auto it = links.find(pdfPage);
        if (it == links.end()) it = links.emplace(pdfPage, navigation.Links(pdfPage)).first;
        return it->second;
    }

    void PdfLinks::Tapped(int page, inkpages::PointF point)
    {
        auto doc = view.Document();
        auto background = view.Pdf();
        auto navigation = background ? background->Navigation() : nullptr;
        if (!doc || !navigation || page < 0 || page >= static_cast<int>(doc->Pages().size())) return;
        auto const& content = *doc->Pages()[page];
        auto shown = [background](int pdfPage) { return background->ShownSize(pdfPage); };
        auto at = pdf::ToShown(content, { point.x, point.y }, shown);
        if (!at) return;
        keep = navigation;
        for (auto const& link : LinksOf(*navigation, content.PdfPageIndex()))
        {
            if (!Contains(link.area, *at)) continue;
            if (auto destination = std::get_if<pdf::Destination>(&link.target)) Go(*destination);
            else OpenUri(std::get<std::wstring>(link.target));
            return;
        }
    }

    void PdfLinks::OpenUri(std::wstring const& uri)
    {
        if (!IsWebAddress(uri))
        {
            Log(std::format("pdf links: not opened (not a web or mail address): {}", ToUtf8(uri)));
            Dialogs::ShowError(owner, L"Dieser Link wird nicht geöffnet.",
                std::format(L"Nur Web- und E-Mail-Adressen werden geöffnet:\n{}", uri));
            return;
        }
        if (!Dialogs::Confirm(owner, L"Link öffnen?", uri, L"Öffnen")) return;
        SHELLEXECUTEINFOW info{ sizeof(info) };
        info.fMask = SEE_MASK_NOASYNC;
        info.hwnd = owner;
        info.lpVerb = L"open";
        info.lpFile = uri.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info))
        {
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            LogError(std::format("pdf links: opening {}: {}", ToUtf8(uri), DescribeHr(hr)));
            Dialogs::ShowError(owner, L"Der Link kann nicht geöffnet werden.", std::format(L"{}\n{}", uri, FromUtf8(DescribeHr(hr))));
        }
    }
}
