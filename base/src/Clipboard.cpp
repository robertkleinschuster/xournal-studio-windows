#include "xournal/base/Clipboard.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"

#include <cstring>
#include <format>

namespace xournal::Clipboard
{
    Opened::Opened(HWND owner)
    {
        if (!OpenClipboard(owner)) ThrowLastError("OpenClipboard");
    }

    Opened::~Opened()
    {
        if (!CloseClipboard())
            LogError(std::format("CloseClipboard: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
    }

    void Opened::Clear() const
    {
        if (!EmptyClipboard()) ThrowLastError("EmptyClipboard");
    }

    void Opened::SetData(UINT format, void const* bytes, size_t size) const
    {
        struct OwnedMemory
        {
            HGLOBAL handle;
            ~OwnedMemory()
            {
                if (handle && GlobalFree(handle))
                    LogError(std::format("GlobalFree (clipboard): {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
            }
        } memory{ GlobalAlloc(GMEM_MOVEABLE, size) };
        if (!memory.handle) ThrowLastError("GlobalAlloc (clipboard)");
        void* target = GlobalLock(memory.handle);
        if (!target) ThrowLastError("GlobalLock (clipboard)");
        std::memcpy(target, bytes, size);
        if (!GlobalUnlock(memory.handle))
        {
            DWORD error = GetLastError();
            if (error != NO_ERROR) ThrowHr("GlobalUnlock (clipboard)", HRESULT_FROM_WIN32(error));
        }
        if (!SetClipboardData(format, memory.handle))
        {
            DWORD error = GetLastError();
            ThrowHr(std::format("SetClipboardData({})", format), HRESULT_FROM_WIN32(error));
        }
        memory.handle = nullptr; // Windows owns the memory now.
    }

    void PutText(HWND owner, std::wstring const& text)
    {
        Opened clipboard(owner);
        clipboard.Clear();
        clipboard.SetData(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    }
}
