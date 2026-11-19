#pragma once

// Messages as a toast: a small popup window at the bottom of the writing surface (its own layered window, so it lies
// above the ink's composition). Success closes after 5 s; an error stays until it is clicked (it is in errors.log too).

#include <windows.h>

#include <string>

namespace xournal::app
{
    enum class ToastKind { Success, Info, Error };

    class Toast
    {
    public:
        explicit Toast(HWND owner);
        ~Toast();
        Toast(Toast const&) = delete;
        Toast& operator=(Toast const&) = delete;

        void Show(std::wstring text, ToastKind kind);
        /// The area it centers at the bottom of (screen pixels): the writing surface. Call when the window moves or sizes.
        void Place(RECT area);

        HWND Window() const { return window; }

    private:
        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
        LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
        void Layout();
        void Paint();
        HFONT Font();

        HWND owner;
        HWND window = nullptr;
        RECT area{};
        std::wstring text;
        ToastKind kind = ToastKind::Info;
        HFONT font = nullptr;
        UINT fontDpi = 0;
    };
}
