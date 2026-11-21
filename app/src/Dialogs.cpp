#include "Dialogs.h"
#include "DarkColors.h"
#include "NativeTheme.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <format>
#include <span>

using Microsoft::WRL::ComPtr;

namespace xournal::app::Dialogs
{
    void SetState(HWND dialog, LPARAM state)
    {
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(dialog, DWLP_USER, state) && GetLastError() != ERROR_SUCCESS)
            ThrowLastError("SetWindowLongPtr (dialog state)");
        if (GetPropW(GetAncestor(dialog, GA_ROOTOWNER), DarkColors::WindowProperty))
        {
            NativeTheme::ApplyDialog(dialog);
        }
    }

    bool DrawDarkPushButton(NMHDR const& header)
    {
        if (header.code != NM_CUSTOMDRAW) return false;
        HWND button = header.hwndFrom;
        wchar_t type[16]{};
        if (!GetClassNameW(button, type, ARRAYSIZE(type)) || wcscmp(type, WC_BUTTONW) != 0) return false;
        auto style = GetWindowLongPtrW(button, GWL_STYLE) & BS_TYPEMASK;
        if (style != BS_PUSHBUTTON && style != BS_DEFPUSHBUTTON) return false;
        auto const& draw = reinterpret_cast<NMCUSTOMDRAW const&>(header);
        if (draw.dwDrawStage != CDDS_PREPAINT) return false;

        wchar_t caption[512]{};
        if (!GetWindowTextW(button, caption, ARRAYSIZE(caption))) return false;
        HDC dc = draw.hdc;
        int saved = SaveDC(dc);
        if (!saved) ThrowHr("SaveDC (dark button)", E_FAIL);
        auto brush = static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
        bool focused = (draw.uItemState & CDIS_FOCUS) != 0 || GetFocus() == button;
        bool selected = (draw.uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
        SetDCBrushColor(dc, selected ? DarkColors::Selected : DarkColors::Input);
        FillRect(dc, &draw.rc, brush);
        SetDCBrushColor(dc, focused || style == BS_DEFPUSHBUTTON ? RGB(134, 181, 255) : RGB(98, 102, 110));
        FrameRect(dc, &draw.rc, brush);
        auto font = reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0));
        if (font) SelectObject(dc, font);
        SetTextColor(dc, IsWindowEnabled(button) ? DarkColors::Text : RGB(157, 159, 165));
        SetBkMode(dc, TRANSPARENT);
        UINT flags = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
        auto cues = SendMessageW(button, WM_QUERYUISTATE, 0, 0);
        if (cues & UISF_HIDEACCEL) flags |= DT_HIDEPREFIX;
        RECT label = draw.rc;
        InflateRect(&label, -4, -2);
        DrawTextW(dc, caption, -1, &label, flags);
        if (focused && !(cues & UISF_HIDEFOCUS)) DrawFocusRect(dc, &label);
        if (!RestoreDC(dc, saved)) LogError("RestoreDC (dark button)");
        return true;
    }

    void SetDarkCalendarPalette(NMHDR const& header)
    {
        if (header.code != DTN_DROPDOWN) return;
        HWND calendar = DateTime_GetMonthCal(header.hwndFrom);
        if (!calendar) ThrowHr("DTM_GETMONTHCAL (date picker)", E_FAIL);
        // The picker creates a fresh native calendar for each drop-down.
        Check("SetWindowTheme (month calendar)", SetWindowTheme(calendar, L"", L""));
        for (auto [part, color] : { std::pair{ MCSC_BACKGROUND, DarkColors::Background },
                std::pair{ MCSC_MONTHBK, DarkColors::Input }, std::pair{ MCSC_TEXT, DarkColors::Text },
                std::pair{ MCSC_TITLEBK, DarkColors::Selected }, std::pair{ MCSC_TITLETEXT, DarkColors::Text },
                std::pair{ MCSC_TRAILINGTEXT, RGB(170, 172, 178) } })
        {
            if (MonthCal_SetColor(calendar, part, color) == CLR_NONE)
                ThrowHr("MCM_SETCOLOR (month calendar)", E_FAIL);
        }
    }

    INT_PTR ControlColor(HWND dialog, UINT message, WPARAM wp, LPARAM lp)
    {
        if (message != WM_CTLCOLORDLG && message != WM_CTLCOLORSTATIC && message != WM_CTLCOLOREDIT &&
            message != WM_CTLCOLORLISTBOX && message != WM_CTLCOLORBTN && message != WM_NOTIFY) return 0;
        if (!GetPropW(GetAncestor(dialog, GA_ROOTOWNER), DarkColors::WindowProperty)) return 0;
        if (message == WM_NOTIFY)
        {
            auto const& note = *reinterpret_cast<NMHDR const*>(lp);
            SetDarkCalendarPalette(note);
            if (!DrawDarkPushButton(note)) return 0;
            SetLastError(ERROR_SUCCESS);
            if (!SetWindowLongPtrW(dialog, DWLP_MSGRESULT, CDRF_SKIPDEFAULT) && GetLastError() != ERROR_SUCCESS)
                ThrowLastError("SetWindowLongPtr (dark dialog button)");
            return TRUE;
        }
        auto context = reinterpret_cast<HDC>(wp);
        COLORREF bg = (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) ? DarkColors::Input : DarkColors::Background;
        SetTextColor(context, DarkColors::Text);
        SetBkColor(context, bg);
        SetDCBrushColor(context, bg);
        return reinterpret_cast<INT_PTR>(GetStockObject(DC_BRUSH));
    }

    namespace
    {
        // Shows a file dialog; false when the user cancelled (any other failure throws).
        bool ShowDialog(IFileDialog* dialog, HWND owner)
        {
            HRESULT hr = dialog->Show(owner);
            if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            {
                return false;
            }
            Check("IFileDialog::Show", hr);
            return true;
        }

        void SetFolder(IFileDialog* dialog, std::optional<std::filesystem::path> const& folder)
        {
            if (!folder || !std::filesystem::is_directory(*folder))
            {
                return;
            }
            ComPtr<IShellItem> item;
            Check("SHCreateItemFromParsingName", SHCreateItemFromParsingName(folder->c_str(), nullptr, IID_PPV_ARGS(&item)));
            Check("IFileDialog::SetFolder", dialog->SetFolder(item.Get()));
        }

        std::filesystem::path ResultPath(IFileDialog* dialog)
        {
            ComPtr<IShellItem> item;
            Check("IFileDialog::GetResult", dialog->GetResult(&item));
            PWSTR path = nullptr;
            Check("IShellItem::GetDisplayName", item->GetDisplayName(SIGDN_FILESYSPATH, &path));
            std::filesystem::path result(path);
            CoTaskMemFree(path);
            return result;
        }
    }

    namespace
    {
        // Windows' open dialog for these file types.
        std::optional<std::filesystem::path> OpenFile(HWND owner, std::optional<std::filesystem::path> const& folder,
            std::span<COMDLG_FILTERSPEC const> types, wchar_t const* title)
        {
            ComPtr<IFileOpenDialog> dialog;
            Check("CoCreateInstance(FileOpenDialog)", CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
            Check("IFileDialog::SetFileTypes", dialog->SetFileTypes(static_cast<UINT>(types.size()), types.data()));
            Check("IFileDialog::SetTitle", dialog->SetTitle(title));
            SetFolder(dialog.Get(), folder);
            if (!ShowDialog(dialog.Get(), owner))
            {
                return std::nullopt;
            }
            return ResultPath(dialog.Get());
        }
    }

    std::optional<std::filesystem::path> OpenDocument(HWND owner, std::optional<std::filesystem::path> const& folder)
    {
        COMDLG_FILTERSPEC types[] = {
            { L"Dokumente und PDF", L"*.xopp;*.moj;*.pdf" },
            { L"Xournal++-Dokumente", L"*.xopp" },
            { L"MrWriter-Dokumente", L"*.moj" },
            { L"PDF", L"*.pdf" },
        };
        return OpenFile(owner, folder, types, L"Öffnen");
    }

    std::optional<std::filesystem::path> OpenPdf(HWND owner, std::optional<std::filesystem::path> const& folder, wchar_t const* title)
    {
        COMDLG_FILTERSPEC types[] = { { L"PDF", L"*.pdf" } };
        return OpenFile(owner, folder, types, title);
    }

    std::optional<std::filesystem::path> OpenPagesSource(HWND owner, std::optional<std::filesystem::path> const& folder)
    {
        COMDLG_FILTERSPEC types[] = {
            { L"PDF und Xournal++-Dokumente", L"*.pdf;*.xopp" },
            { L"PDF", L"*.pdf" },
            { L"Xournal++-Dokumente", L"*.xopp" },
        };
        return OpenFile(owner, folder, types, L"Seiten einfügen aus");
    }

    bool Confirm(HWND owner, std::wstring const& instruction, std::wstring const& detail, wchar_t const* action)
    {
        TASKDIALOG_BUTTON buttons[] = { { IDYES, action }, { IDCANCEL, L"Abbrechen" } };
        TASKDIALOGCONFIG config{ sizeof(config) };
        config.hwndParent = owner;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        config.pszWindowTitle = L"Xournal Studio";
        config.pszMainIcon = TD_WARNING_ICON;
        config.pszMainInstruction = instruction.c_str();
        config.pszContent = detail.c_str();
        config.cButtons = ARRAYSIZE(buttons);
        config.pButtons = buttons;
        config.nDefaultButton = IDCANCEL;
        int pressed = 0;
        Check("TaskDialogIndirect (confirm)", NativeTheme::ShowTaskDialog(config, &pressed));
        return pressed == IDYES;
    }

    namespace
    {
        // Windows' save dialog for one file type.
        std::optional<std::filesystem::path> SaveFile(HWND owner, std::filesystem::path const& folder, std::wstring const& name,
            COMDLG_FILTERSPEC type, wchar_t const* extension, wchar_t const* title)
        {
            ComPtr<IFileSaveDialog> dialog;
            Check("CoCreateInstance(FileSaveDialog)", CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
            Check("IFileDialog::SetFileTypes", dialog->SetFileTypes(1, &type));
            Check("IFileDialog::SetDefaultExtension", dialog->SetDefaultExtension(extension));
            Check("IFileDialog::SetTitle", dialog->SetTitle(title));
            Check("IFileDialog::SetFileName", dialog->SetFileName(name.c_str()));
            SetFolder(dialog.Get(), folder);
            if (!ShowDialog(dialog.Get(), owner))
            {
                return std::nullopt;
            }
            return ResultPath(dialog.Get());
        }
    }

    std::optional<std::filesystem::path> SaveDocument(HWND owner, std::filesystem::path const& folder, std::wstring const& name)
    {
        return SaveFile(owner, folder, name, { L"Xournal++-Dokument", L"*.xopp" }, L"xopp", L"Speichern unter");
    }

    std::optional<std::filesystem::path> SavePdf(HWND owner, std::filesystem::path const& folder, std::wstring const& name)
    {
        return SaveFile(owner, folder, name, { L"PDF", L"*.pdf" }, L"pdf", L"Als PDF exportieren");
    }

    std::optional<std::filesystem::path> PickFolder(HWND owner, std::optional<std::filesystem::path> const& folder)
    {
        ComPtr<IFileOpenDialog> dialog;
        Check("CoCreateInstance(FileOpenDialog)", CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));
        FILEOPENDIALOGOPTIONS options = 0;
        Check("IFileDialog::GetOptions", dialog->GetOptions(&options));
        Check("IFileDialog::SetOptions", dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM));
        Check("IFileDialog::SetTitle", dialog->SetTitle(L"Ordner öffnen"));
        SetFolder(dialog.Get(), folder);
        if (!ShowDialog(dialog.Get(), owner))
        {
            return std::nullopt;
        }
        return ResultPath(dialog.Get());
    }

    LeaveChoice AskLeave(HWND owner, std::wstring const& documentName)
    {
        enum { Save = 100, Discard, Cancel };
        TASKDIALOG_BUTTON buttons[] = { { Save, L"Speichern" }, { Discard, L"Nicht speichern" }, { Cancel, L"Abbrechen" } };
        std::wstring instruction = std::format(L"Änderungen an „{}“ speichern?", documentName);
        TASKDIALOGCONFIG config{ sizeof(config) };
        config.hwndParent = owner;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        config.pszWindowTitle = L"Xournal Studio";
        config.pszMainInstruction = instruction.c_str();
        config.pszContent = L"Ohne Speichern gehen die Änderungen verloren.";
        config.cButtons = ARRAYSIZE(buttons);
        config.pButtons = buttons;
        config.nDefaultButton = Save;
        int pressed = 0;
        Check("TaskDialogIndirect", NativeTheme::ShowTaskDialog(config, &pressed));
        return pressed == Save ? LeaveChoice::Save : pressed == Discard ? LeaveChoice::Discard : LeaveChoice::Cancel;
    }

    void ShowError(HWND owner, std::wstring const& title, std::wstring const& detail)
    {
        TASKDIALOGCONFIG config{ sizeof(config) };
        config.hwndParent = owner;
        config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        config.pszWindowTitle = L"Xournal Studio";
        config.pszMainIcon = TD_ERROR_ICON;
        config.pszMainInstruction = title.c_str();
        config.pszContent = detail.c_str();
        config.dwCommonButtons = TDCBF_OK_BUTTON;
        HRESULT hr = NativeTheme::ShowTaskDialog(config, nullptr);
        if (FAILED(hr))
        {
            LogError(std::format("TaskDialogIndirect (error dialog): {}", DescribeHr(hr))); // the error itself is logged by its thrower
        }
    }
}
