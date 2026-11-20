#include "ViewNavigation.h"

#include "GoToPageDialog.h"

#include "xournal/base/Log.h"

#include <cmath>
#include <format>

namespace xournal::app
{
    namespace
    {
        // Page units are points; InkPages shows a page unit as a DIP at zoom 1: the real size is zoom 96/72.
        constexpr float RealSizeZoom = 96.f / 72.f;
        constexpr float ZoomStep = 1.25f;
    }

    void ViewNavigation::Remember()
    {
        if (auto here = surface.CurrentPlace()) history.Visit(*here);
    }

    void ViewNavigation::Jump(std::function<void()> const& go)
    {
        Remember();
        go();
    }

    void ViewNavigation::Back()
    {
        auto here = surface.CurrentPlace();
        if (!here) return;
        if (auto to = history.Back(*here))
        {
            Log(std::format("navigation: back to page {}", to->page + 1));
            surface.GoTo(*to);
        }
    }

    void ViewNavigation::Forward()
    {
        auto here = surface.CurrentPlace();
        if (!here) return;
        if (auto to = history.Forward(*here))
        {
            Log(std::format("navigation: forward to page {}", to->page + 1));
            surface.GoTo(*to);
        }
    }

    void ViewNavigation::GoToPage()
    {
        int count = surface.Layout().Count();
        auto page = GoToPageDialog::Ask(owner, surface.PageInView().value_or(0), count);
        if (page) Jump([this, page] { surface.ScrollToPage(*page); });
    }

    void ViewNavigation::ZoomIn() { surface.ZoomTo(surface.CurrentView().zoom * ZoomStep); }
    void ViewNavigation::ZoomOut() { surface.ZoomTo(surface.CurrentView().zoom / ZoomStep); }
    void ViewNavigation::ActualSize() { surface.ZoomTo(RealSizeZoom); }

    int ViewNavigation::ZoomPercent() const
    {
        return static_cast<int>(std::lround(surface.CurrentView().zoom / RealSizeZoom * 100));
    }
}
