#pragma once

// The mouse over a PDF link: the hand cursor (InkPages' cursorAt) and a tooltip below the pointer with what the link leads
// to - the web address or "Seite n" - as PDF viewers show it. A comctl32 tracking tooltip: shown while the mouse is over
// the link, gone when it leaves (the pen and touch never show it; InkPages calls only for the real mouse).

#include "PdfLinks.h"

#include <windows.h>

#include <string>

namespace xournal::app
{
    class LinkHover
    {
    public:
        LinkHover(HWND owner, PdfLinks& links);
        ~LinkHover();
        LinkHover(LinkHover const&) = delete;
        LinkHover& operator=(LinkHover const&) = delete;

        /// InkPages' cursorAt: the hand over a link (and its tooltip), else the default (and no tooltip).
        LPCWSTR CursorAt(int page, inkpages::PointF point);
        void Hide();
        void SetDarkMode(bool dark);

    private:
        void Show(std::wstring const& text);

        HWND owner;
        PdfLinks& links;
        HWND tip = nullptr;
        std::wstring text; // shown now (empty: hidden)
    };
}
