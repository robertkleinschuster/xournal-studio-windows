#include "xournal/project/ProjectFiles.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"

#include <shellapi.h> // FOF_* (WIN32_LEAN_AND_MEAN leaves it out)
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <array>
#include <format>

using Microsoft::WRL::ComPtr;

namespace xournal::project::ProjectFiles
{
    namespace
    {
        constexpr std::array<std::wstring_view, 22> ReservedNames = { L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2",
            L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5",
            L"LPT6", L"LPT7", L"LPT8", L"LPT9" };

        // The characters .NET's Path.GetInvalidFileNameChars names on Windows: control characters and \ / : * ? " < > |.
        bool IsInvalidNameChar(wchar_t c)
        {
            return c < 32 || std::wstring_view(L"\\/:*?\"<>|").find(c) != std::wstring_view::npos;
        }

        std::string Name(path const& p) { return ToUtf8(p.native()); }

        // Exists as a file or folder; "not found" is no; any other failure (access denied to the folder) is thrown.
        bool Exists(path const& p)
        {
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                return true;
            }
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            {
                return false;
            }
            ThrowHr(std::format("attributes of {}", Name(p)), HRESULT_FROM_WIN32(error));
        }
    }

    std::optional<std::wstring> ValidateName(std::wstring_view name)
    {
        if (Trim(name).empty())
        {
            return L"Der Name darf nicht leer sein.";
        }
        for (wchar_t c : name)
        {
            if (IsInvalidNameChar(c))
            {
                return L"Ein Name darf keines dieser Zeichen enthalten: \\ / : * ? \" < > |";
            }
        }
        if (name.back() == L'.' || name.back() == L' ')
        {
            return L"Ein Name darf nicht mit einem Punkt oder Leerzeichen enden.";
        }
        std::wstring_view stem = name.substr(0, name.find(L'.'));
        while (!stem.empty() && IsSpace(stem.back()))
        {
            stem.remove_suffix(1);
        }
        for (std::wstring_view reserved : ReservedNames)
        {
            if (EqualsIgnoreCase(stem, reserved))
            {
                return std::format(L"\u201E{}\u201C ist unter Windows als Name reserviert.", stem);
            }
        }
        return std::nullopt;
    }

    std::wstring UniqueName(path const& folder, std::wstring_view name, std::wstring_view extension)
    {
        for (int i = 1;; i++)
        {
            std::wstring candidate = (i == 1 ? std::wstring(name) : std::format(L"{} ({})", name, i)) + std::wstring(extension);
            if (!Exists(folder / candidate))
            {
                return candidate;
            }
        }
    }

    bool IsAtOrBelow(std::optional<path> const& p, path const& item)
    {
        return p && Paths::IsAtOrBelow(*p, item);
    }

    path MapRenamed(path const& p, path const& item, path const& renamed)
    {
        if (!IsAtOrBelow(p, item))
        {
            return p;
        }
        std::wstring full = Paths::WithoutTrailingSeparator(p).native(), at = Paths::WithoutTrailingSeparator(item).native();
        return Paths::WithoutTrailingSeparator(renamed).native() + full.substr(at.size());
    }

    std::wstring NewName(path const& p, bool isFolder, std::wstring_view editedName)
    {
        return isFolder ? std::wstring(editedName) : std::wstring(editedName) + p.extension().native();
    }

    std::wstring EditableName(path const& p, bool isFolder)
    {
        return isFolder ? path(Paths::WithoutTrailingSeparator(p)).filename().native() : p.stem().native();
    }

    bool SamePath(std::optional<path> const& a, std::optional<path> const& b)
    {
        return a && b && Paths::Same(*a, *b);
    }

    path Rename(path const& item, std::wstring_view newName)
    {
        path from = Paths::WithoutTrailingSeparator(item);
        path to = from.parent_path() / newName;
        if (!MoveFileExW(from.c_str(), to.c_str(), 0))
        {
            ThrowLastError(std::format("rename {} to {}", Name(from), ToUtf8(newName)));
        }
        return to;
    }

    path CreateFolder(path const& parent, std::wstring_view name)
    {
        path folder = Paths::Full(parent) / name;
        if (!CreateDirectoryW(folder.c_str(), nullptr))
        {
            ThrowLastError(std::format("create folder {}", Name(folder)));
        }
        return folder;
    }

    void Recycle(path const& item, HWND owner)
    {
        Recycle(std::vector<path>{ item }, owner);
    }

    void Recycle(std::vector<path> const& items, HWND owner)
    {
        if (items.empty())
        {
            return;
        }
        std::string step = items.size() == 1 ? std::format("move {} to the recycle bin", Name(items.front()))
                                             : std::format("move {} items to the recycle bin", items.size());
        ComPtr<IFileOperation> operation;
        Check(step + " (CoCreateInstance FileOperation)",
            CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation)));
        // No progress or error UI of its own (the app reports errors); FOF_WANTNUKEWARNING keeps Windows' question
        // before an item that does not fit the recycle bin would be deleted for good.
        Check(step + " (SetOperationFlags)", operation->SetOperationFlags(FOF_SILENT | FOF_NOCONFIRMATION |
            FOF_NOERRORUI | FOF_WANTNUKEWARNING | FOFX_RECYCLEONDELETE | FOFX_EARLYFAILURE));
        if (owner)
        {
            Check(step + " (SetOwnerWindow)", operation->SetOwnerWindow(owner));
        }
        for (path const& item : items)
        {
            ComPtr<IShellItem> shellItem;
            Check(std::format("{} (SHCreateItemFromParsingName {})", step, Name(item)),
                SHCreateItemFromParsingName(Paths::WithoutTrailingSeparator(item).c_str(), nullptr, IID_PPV_ARGS(&shellItem)));
            Check(step + " (DeleteItem)", operation->DeleteItem(shellItem.Get(), nullptr));
        }
        Check(step, operation->PerformOperations());
        BOOL aborted = FALSE;
        Check(step + " (GetAnyOperationsAborted)", operation->GetAnyOperationsAborted(&aborted));
        if (aborted)
        {
            ThrowHr(step + " (declined)", E_ABORT);
        }
    }

    void RecycleCopies(std::vector<path> const& files, HWND owner)
    {
        if (files.empty())
        {
            return;
        }
        wchar_t temp[MAX_PATH + 1]{};
        DWORD length = GetTempPathW(MAX_PATH + 1, temp);
        if (length == 0 || length > MAX_PATH)
        {
            ThrowLastError("GetTempPath");
        }
        path folder = path(temp) / std::format(L"XournalStudio-Fassungen-{}-{}", GetCurrentProcessId(), GetTickCount64());
        if (!CreateDirectoryW(folder.c_str(), nullptr))
        {
            ThrowLastError(std::format("create folder {}", Name(folder)));
        }
        struct Cleanup
        {
            path folder;
            ~Cleanup()
            {
                std::error_code error;
                std::filesystem::remove_all(folder, error);
                if (error)
                {
                    LogError(std::format("remove {}: {}", ToUtf8(folder.native()), error.message()));
                }
            }
        } cleanup{ folder };
        std::vector<path> copies;
        for (size_t i = 0; i < files.size(); i++)
        {
            path target = folder;
            if (Exists(folder / files[i].filename()))
            {
                // Two files of the same name: each copy keeps its name in a folder of its own.
                target = folder / std::to_wstring(i);
                if (!CreateDirectoryW(target.c_str(), nullptr))
                {
                    ThrowLastError(std::format("create folder {}", Name(target)));
                }
            }
            target /= files[i].filename();
            if (!CopyFileW(Paths::Full(files[i]).c_str(), target.c_str(), TRUE))
            {
                ThrowLastError(std::format("copy {} for the recycle bin", Name(files[i])));
            }
            copies.push_back(target);
        }
        Recycle(copies, owner);
    }
}
