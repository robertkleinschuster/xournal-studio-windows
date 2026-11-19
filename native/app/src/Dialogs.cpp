#include "Dialogs.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <commctrl.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <format>
#include <span>

using Microsoft::WRL::ComPtr;

namespace xournal::app::Dialogs
{
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
        Check("TaskDialogIndirect (confirm)", TaskDialogIndirect(&config, &pressed, nullptr, nullptr));
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
        Check("TaskDialogIndirect", TaskDialogIndirect(&config, &pressed, nullptr, nullptr));
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
        HRESULT hr = TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
        if (FAILED(hr))
        {
            LogError(std::format("TaskDialogIndirect (error dialog): {}", DescribeHr(hr))); // the error itself is logged by its thrower
        }
    }
}
