#include "xournal/base/Paths.h"

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"

#include <windows.h>

#include <algorithm>
#include <format>
#include <vector>

namespace xournal::Paths
{
    path Full(path const& p)
    {
        std::wstring const& text = p.native();
        DWORD length = GetFullPathNameW(text.c_str(), 0, nullptr, nullptr);
        if (length == 0)
        {
            ThrowLastError(std::format("GetFullPathName {}", ToUtf8(text)));
        }
        std::wstring full(length, L'\0');
        length = GetFullPathNameW(text.c_str(), length, full.data(), nullptr);
        if (length == 0)
        {
            ThrowLastError(std::format("GetFullPathName {}", ToUtf8(text)));
        }
        full.resize(length);
        return full;
    }

    bool Same(path const& a, path const& b)
    {
        return EqualsIgnoreCase(Full(a).native(), Full(b).native());
    }

    path WithoutTrailingSeparator(path const& p)
    {
        std::wstring full = Full(p).native();
        while (full.size() > 1 && (full.back() == L'\\' || full.back() == L'/') && path(full).root_path().native() != full)
        {
            full.pop_back();
        }
        return full;
    }

    bool IsAtOrBelow(path const& p, path const& item)
    {
        std::wstring full = WithoutTrailingSeparator(p).native(), at = WithoutTrailingSeparator(item).native();
        if (EqualsIgnoreCase(full, at))
        {
            return true;
        }
        bool rootItem = !at.empty() && (at.back() == L'\\' || at.back() == L'/'); // a drive root holds everything on it
        return full.size() > at.size() && StartsWithIgnoreCase(full, at) && (rootItem || full[at.size()] == L'\\');
    }

    path DirectoryOf(path const& file)
    {
        return Full(file).parent_path();
    }

    path Relative(path const& base, path const& target)
    {
        path from = Full(base), to = Full(target);
        if (!EqualsIgnoreCase(from.root_name().native(), to.root_name().native()))
        {
            return to;
        }
        std::vector<std::wstring> fromParts, toParts;
        for (auto const& part : from.relative_path())
        {
            if (!part.empty())
            {
                fromParts.push_back(part.native());
            }
        }
        for (auto const& part : to.relative_path())
        {
            if (!part.empty())
            {
                toParts.push_back(part.native());
            }
        }
        size_t common = 0;
        while (common < fromParts.size() && common < toParts.size() && EqualsIgnoreCase(fromParts[common], toParts[common]))
        {
            ++common;
        }
        path relative;
        for (size_t i = common; i < fromParts.size(); ++i)
        {
            relative /= L"..";
        }
        for (size_t i = common; i < toParts.size(); ++i)
        {
            relative /= toParts[i];
        }
        return relative.empty() ? path(L".") : relative;
    }

    bool IsRooted(std::wstring const& p)
    {
        path value(p);
        return value.has_root_name() || value.has_root_directory();
    }

    std::wstring WithForwardSlashes(path const& p)
    {
        std::wstring text = p.native();
        std::replace(text.begin(), text.end(), L'\\', L'/');
        return text;
    }

    bool FileExists(path const& p)
    {
        DWORD attributes = GetFileAttributesW(p.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME ||
                error == ERROR_BAD_NETPATH || error == ERROR_BAD_NET_NAME || error == ERROR_NOT_READY)
            {
                return false; // nothing there (or a drive that is not there): the caller handles a missing file
            }
            SetLastError(error);
            ThrowLastError(std::format("GetFileAttributes {}", ToUtf8(p.native())));
        }
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }
}
