#pragma once

// The Suche tab: a search field (Enter searches, Enter again or F3 goes to the next hit, Umschalt+Enter or Umschalt+F3
// to the previous one, Esc gives the keyboard back to the page), a line that says how the search stands, and the hits
// in a comctl32 list (page, kind, the hit with some text around it); a click on a hit goes there. Shows what
// DocumentSearch has; asks it through the callbacks.

#include "xournal/search/Search.h"

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace xournal::app
{
    class SearchPane
    {
    public:
        explicit SearchPane(HWND parent);
        ~SearchPane();
        SearchPane(SearchPane const&) = delete;
        SearchPane& operator=(SearchPane const&) = delete;

        HWND Window() const { return window; }

        /// Puts the keyboard into the search field, its text selected.
        void FocusField();

        /// Shows the hits, the current one selected, and how the search stands.
        void Show(std::vector<search::SearchHit> const& hits, std::optional<size_t> current, bool running,
            std::wstring const& query);

        std::function<void(std::wstring const& query)> searchRequested; // Enter with a new query
        std::function<void()> next;                                     // Enter again, F3
        std::function<void()> previous;                                 // Umschalt+Enter, Umschalt+F3
        std::function<void(size_t index)> chosen;                       // a hit clicked
        std::function<void()> leave;                                    // Esc

    private:
        static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
        static LRESULT CALLBACK FieldProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data);
        LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);
        bool OnFieldKey(WPARAM key);
        void Layout();
        std::wstring FieldText() const;

        HWND window = nullptr;
        HWND field = nullptr;
        HWND status = nullptr;
        HWND list = nullptr;
        struct Row { std::wstring page, kind, context; };
        std::vector<Row> rows;          // the list's texts (owner data)
        std::wstring searched;          // the query the shown hits belong to
        bool filling = false;           // the list is being filled: its selection changes are not the user's
    };
}
