#include "xournal/project/ProjectTree.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <windows.h>

#include <algorithm>
#include <format>
#include <memory>

namespace xournal::project
{
    namespace
    {
        struct FindCloser
        {
            void operator()(HANDLE h) const
            {
                if (!FindClose(h))
                {
                    LogError(std::format("FindClose: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
                }
            }
        };

        struct Found
        {
            std::wstring name;
            bool folder = false;
            bool link = false;
        };

        bool IsShown(WIN32_FIND_DATAW const& data)
        {
            return (data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) == 0;
        }

        // Junctions and symbolic links only: other reparse points (cloud files of OneDrive, deduplicated files) are
        // ordinary folders to enter. dwReserved0 holds the reparse tag (documented for WIN32_FIND_DATA).
        bool IsLink(WIN32_FIND_DATAW const& data)
        {
            return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                (data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT || data.dwReserved0 == IO_REPARSE_TAG_SYMLINK);
        }

        std::vector<Found> List(std::filesystem::path const& folder)
        {
            std::wstring pattern = (folder / L"*").native();
            WIN32_FIND_DATAW data{};
            HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                FIND_FIRST_EX_LARGE_FETCH);
            if (h == INVALID_HANDLE_VALUE)
            {
                ThrowLastError(std::format("list folder {}", ToUtf8(folder.native())));
            }
            std::unique_ptr<void, FindCloser> find(h);
            std::vector<Found> found;
            do
            {
                std::wstring_view name = data.cFileName;
                if (name == L"." || name == L".." || !IsShown(data))
                {
                    continue;
                }
                found.push_back({ std::wstring(name), (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, IsLink(data) });
            } while (FindNextFileW(h, &data));
            if (DWORD error = GetLastError(); error != ERROR_NO_MORE_FILES)
            {
                ThrowHr(std::format("list folder {}", ToUtf8(folder.native())), HRESULT_FROM_WIN32(error));
            }
            return found;
        }

        std::vector<ProjectEntry> ReadChildren(std::filesystem::path const& folder)
        {
            std::vector<Found> found;
            try
            {
                found = List(folder);
            }
            catch (Error const&)
            {
                LogException(std::format("read folder {} (shown empty)", ToUtf8(folder.native())));
                return {};
            }
            std::ranges::sort(found, [](Found const& a, Found const& b)
            {
                if (a.folder != b.folder)
                {
                    return a.folder;
                }
                return ProjectTree::CompareNames(a.name, b.name) < 0;
            });
            std::vector<ProjectEntry> entries;
            entries.reserve(found.size());
            for (Found const& item : found)
            {
                auto path = folder / item.name;
                entries.push_back({ path, item.folder, item.folder && !item.link ? ReadChildren(path) : std::vector<ProjectEntry>{} });
            }
            return entries;
        }
    }

    int ProjectTree::CompareNames(std::wstring_view a, std::wstring_view b)
    {
        int result = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.data(),
            static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), nullptr, nullptr, 0);
        if (result == 0)
        {
            ThrowLastError("CompareStringEx");
        }
        if (result != CSTR_EQUAL)
        {
            return result - CSTR_EQUAL;
        }
        return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), FALSE) -
            CSTR_EQUAL;
    }

    ProjectEntry ProjectTree::Read(std::filesystem::path const& folder)
    {
        return { folder, true, ReadChildren(folder) };
    }

    void ProjectTree::AddMissing(ProjectEntry& root, std::vector<std::filesystem::path> const& files)
    {
        auto before = [](ProjectEntry const& a, ProjectEntry const& b)
        {
            return a.isFolder != b.isFolder ? a.isFolder : CompareNames(a.Name(), b.Name()) < 0;
        };
        for (auto const& file : files)
        {
            // Below the root, case ignored like the file system (git's and the settings' spelling may differ).
            std::wstring base = root.path.native();
            while (!base.empty() && (base.back() == L'\\' || base.back() == L'/'))
            {
                base.pop_back();
            }
            std::wstring const& full = file.native();
            if (full.size() <= base.size() + 1 || !StartsWithIgnoreCase(full, base) || full[base.size()] != L'\\')
            {
                continue;
            }
            std::filesystem::path relative = full.substr(base.size() + 1);
            ProjectEntry* folder = &root;
            std::filesystem::path at = root.path;
            std::vector<std::filesystem::path> parts(relative.begin(), relative.end());
            for (size_t i = 0; i < parts.size(); i++)
            {
                at /= parts[i];
                bool isFolder = i + 1 < parts.size();
                auto& children = folder->children;
                auto found = std::ranges::find_if(children, [&](ProjectEntry const& e)
                {
                    return e.isFolder == isFolder && EqualsIgnoreCase(e.Name(), parts[i].native());
                });
                if (found == children.end())
                {
                    ProjectEntry entry{ at, isFolder, {}, true };
                    found = children.insert(std::ranges::upper_bound(children, entry, before), std::move(entry));
                }
                folder = &*found;
            }
        }
    }
}
