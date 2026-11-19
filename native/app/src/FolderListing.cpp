#include "FolderListing.h"

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <format>
#include <memory>

namespace xournal::app::FolderListing
{
    namespace
    {
        struct FindCloser
        {
            void operator()(HANDLE h) const { FindClose(h); }
        };
    }

    std::vector<FolderEntry> List(std::filesystem::path const& folder)
    {
        std::vector<FolderEntry> entries;
        WIN32_FIND_DATAW data{};
        std::wstring pattern = (folder / L"*").native();
        HANDLE found = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (found == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) return entries; // empty
            ThrowLastError(std::format("list folder {}", ToUtf8(folder.native())));
        }
        std::unique_ptr<void, FindCloser> handle(found);
        do
        {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..") continue;
            if (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
            bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            bool link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            entries.push_back({ name, folder / name, directory && !link });
        } while (FindNextFileW(found, &data));
        if (GetLastError() != ERROR_NO_MORE_FILES)
        {
            ThrowLastError(std::format("list folder {}", ToUtf8(folder.native())));
        }
        std::sort(entries.begin(), entries.end(), [](FolderEntry const& a, FolderEntry const& b)
        {
            if (a.folder != b.folder) return a.folder;
            return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
        });
        return entries;
    }
}
