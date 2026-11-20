#include "xournal/project/FileClipboard.h"

#include "xournal/base/Clipboard.h"
#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <oleidl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstring>
#include <format>

namespace xournal::project::FileClipboard
{
    namespace
    {
        UINT DropEffectFormat()
        {
            UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
            if (!format)
            {
                ThrowLastError("RegisterClipboardFormat(Preferred DropEffect)");
            }
            return format;
        }

    }

    std::vector<std::uint8_t> DropBlock(std::vector<path> const& files)
    {
        std::wstring list;
        for (path const& file : files)
        {
            list += file.native();
            list += L'\0';
        }
        list += L'\0';
        DROPFILES header{};
        header.pFiles = sizeof(DROPFILES);
        header.fWide = TRUE;
        std::vector<std::uint8_t> block(sizeof(DROPFILES) + list.size() * sizeof(wchar_t));
        std::memcpy(block.data(), &header, sizeof(header));
        std::memcpy(block.data() + sizeof(header), list.data(), list.size() * sizeof(wchar_t));
        return block;
    }

    std::vector<path> FilesOf(HDROP drop)
    {
        std::vector<path> files;
        UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; i++)
        {
            UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring name(length + 1, L'\0');
            if (DragQueryFileW(drop, i, name.data(), length + 1) != length)
            {
                ThrowHr(std::format("DragQueryFile {}", i), E_FAIL);
            }
            name.resize(length);
            files.emplace_back(std::move(name));
        }
        return files;
    }

    void Put(HWND owner, std::vector<path> const& files, bool move)
    {
        std::vector<std::uint8_t> block = DropBlock(files);
        DWORD effect = move ? DROPEFFECT_MOVE : DROPEFFECT_COPY | DROPEFFECT_LINK; // as File Explorer writes it
        UINT effectFormat = DropEffectFormat();
        Clipboard::Opened clipboard(owner);
        clipboard.Clear();
        clipboard.SetData(CF_HDROP, block.data(), block.size());
        clipboard.SetData(effectFormat, &effect, sizeof(effect));
    }

    std::optional<Content> Get(HWND owner)
    {
        if (!IsClipboardFormatAvailable(CF_HDROP))
        {
            return std::nullopt;
        }
        UINT effectFormat = DropEffectFormat();
        Clipboard::Opened clipboard(owner);
        auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
        if (!drop)
        {
            ThrowLastError("GetClipboardData(CF_HDROP)");
        }
        Content content;
        content.files = FilesOf(drop);
        if (HGLOBAL memory = GetClipboardData(effectFormat))
        {
            if (auto* effect = static_cast<DWORD const*>(GlobalLock(memory)))
            {
                content.move = GlobalSize(memory) >= sizeof(DWORD) && (*effect & DROPEFFECT_MOVE) != 0 &&
                    (*effect & DROPEFFECT_COPY) == 0;
                GlobalUnlock(memory);
            }
            else
            {
                LogError(std::format("clipboard: Preferred DropEffect not readable: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
            }
        }
        return content;
    }

    void Clear(HWND owner)
    {
        Clipboard::Opened clipboard(owner);
        clipboard.Clear();
    }
}
