#pragma once

// The InkPages toolbar as this app shows it: German texts, the pen cursors of the exe's resources, and the app's own
// buttons at the end (Seite einfügen, Seite löschen; Einfügen is the library's own selection command).

#include "inkpages/Toolbar.h"

#include <functional>
#include <memory>

namespace xournal::app
{
    struct AppToolbarActions
    {
        std::function<void()> insertPage;
        std::function<void()> deletePage;
    };

    /// The toolbar's German texts (the C# app's PenToolbarTexts.German).
    inkpages::ToolbarTexts GermanToolbarTexts();

    /// Creates the toolbar over `surface`; the caller keeps it.
    std::unique_ptr<inkpages::Toolbar> CreateAppToolbar(HWND parent, inkpages::WritingSurface& surface,
        inkpages::ToolChoices choices, AppToolbarActions actions);
}
