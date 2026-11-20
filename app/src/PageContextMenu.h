#pragma once

// The context menu of the pages (right click, Umschalt+F10, the menu key): what a PDF viewer offers there - the
// selection's commands (Kopieren also for the PDF's selected text, Auswahl aufheben), Zurück/Vorwärts, the zoom commands, Gehe zu Seite. Items carry the menu's command ids and
// shortcuts; the chosen one is returned to run as a menu command.

#include <windows.h>

namespace xournal::app::PageContextMenu
{
    struct State
    {
        bool selection = false;     // ink, pictures, texts (InkPages' selection)
        bool textSelection = false; // the PDF's text (copied only)
        bool canGoBack = false, canGoForward = false;
    };

    /// Shows the menu at `screen` (a keyboard request: (-1, -1) - then at `fallback`); returns the chosen command or 0.
    int Show(HWND owner, POINT screen, POINT fallback, State const& state);
}
