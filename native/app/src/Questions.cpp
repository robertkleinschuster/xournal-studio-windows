#include "Questions.h"

#include "xournal/base/Error.h"

#include <commctrl.h>

namespace xournal::app::Questions
{
    int Choose(HWND owner, std::wstring const& title, std::wstring const& text, std::vector<std::wstring> const& buttons,
        int defaultButton)
    {
        constexpr int FirstId = 100;
        std::vector<TASKDIALOG_BUTTON> items;
        for (size_t i = 0; i < buttons.size(); i++)
        {
            items.push_back({ FirstId + static_cast<int>(i), buttons[i].c_str() });
        }
        TASKDIALOGCONFIG config{ sizeof(config) };
        config.hwndParent = owner;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        config.pszWindowTitle = L"Xournal Studio";
        config.pszMainInstruction = title.c_str();
        config.pszContent = text.c_str();
        config.cButtons = static_cast<UINT>(items.size());
        config.pButtons = items.data();
        config.nDefaultButton = FirstId + defaultButton;
        int pressed = 0;
        Check("TaskDialogIndirect", TaskDialogIndirect(&config, &pressed, nullptr, nullptr));
        return pressed >= FirstId && pressed < FirstId + static_cast<int>(buttons.size()) ? pressed - FirstId : -1;
    }

    bool Confirm(HWND owner, std::wstring const& title, std::wstring const& text, std::wstring const& verb)
    {
        return Choose(owner, title, text, { verb, L"Abbrechen" }, 1) == 0;
    }
}
