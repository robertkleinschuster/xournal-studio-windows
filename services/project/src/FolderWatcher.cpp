#include "xournal/project/FolderWatcher.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"

#include <format>
#include <vector>

namespace xournal::project
{
    namespace
    {
        constexpr DWORD BufferSize = 64 * 1024; // the documented maximum for network shares
        constexpr DWORD Filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE;

        bool IsDirectory(std::filesystem::path const& p)
        {
            DWORD attributes = GetFileAttributesW(p.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        bool IsHiddenPart(std::wstring_view part)
        {
            return part.starts_with(L'.') && part != L"." && part != L"..";
        }
    }

    void FolderWatcher::HandleCloser::operator()(HANDLE h) const
    {
        if (!CloseHandle(h))
        {
            LogError(std::format("CloseHandle: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
        }
    }

    FolderWatcher::FolderWatcher(std::filesystem::path folderToWatch, std::function<void()> onChanged)
        : folder(Paths::Full(folderToWatch)), changed(std::move(onChanged))
    {
        HANDLE dir = CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (dir == INVALID_HANDLE_VALUE)
        {
            ThrowLastError(std::format("watch folder {}", ToUtf8(folder.native())));
        }
        directory.reset(dir);
        HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent)
        {
            ThrowLastError("folder watcher: CreateEvent");
        }
        stop.reset(stopEvent);
        worker = std::thread([this] { Run(); });
    }

    FolderWatcher::~FolderWatcher()
    {
        if (!SetEvent(stop.get()))
        {
            // The worker would never end: a destructor cannot report this but by the log and by not hanging forever.
            LogError(std::format("folder watcher: SetEvent: {} (worker left running)",
                DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
            worker.detach();
            return;
        }
        worker.join();
    }

    bool FolderWatcher::IsInHiddenFolder(std::filesystem::path const& root, std::filesystem::path const& p)
    {
        std::wstring relative = Paths::Relative(root, p).native();
        auto parts = Split(relative, L'\\');
        if (parts.empty())
        {
            return false;
        }
        for (size_t i = 0; i + 1 < parts.size(); i++)
        {
            if (IsHiddenPart(parts[i]))
            {
                return true;
            }
        }
        return IsHiddenPart(parts.back()) && IsDirectory(Paths::Full(p));
    }

    bool FolderWatcher::HasShownChange(std::uint8_t const* buffer) const
    {
        for (DWORD offset = 0;;)
        {
            auto const* info = reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(buffer + offset);
            std::wstring_view name(info->FileName, info->FileNameLength / sizeof(wchar_t));
            if (!IsInHiddenFolder(folder, folder / name))
            {
                return true;
            }
            if (info->NextEntryOffset == 0)
            {
                return false;
            }
            offset += info->NextEntryOffset;
        }
    }

    void FolderWatcher::Raise() noexcept
    {
        try
        {
            changed();
        }
        catch (...)
        {
            LogException("folder watcher: changed handler");
        }
    }

    void FolderWatcher::Run() noexcept
    {
        try
        {
            Handle done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!done)
            {
                ThrowLastError("folder watcher: CreateEvent");
            }
            std::vector<DWORD> storage(BufferSize / sizeof(DWORD)); // DWORD-aligned, as ReadDirectoryChangesW requires
            auto* buffer = reinterpret_cast<std::uint8_t*>(storage.data());
            for (;;)
            {
                OVERLAPPED overlapped{};
                overlapped.hEvent = done.get();
                if (!ReadDirectoryChangesW(directory.get(), buffer, BufferSize, TRUE, Filter, nullptr, &overlapped, nullptr))
                {
                    ThrowLastError(std::format("watch folder {}", ToUtf8(folder.native())));
                }
                HANDLE events[] = { done.get(), stop.get() };
                DWORD signaled = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (signaled == WAIT_FAILED)
                {
                    ThrowLastError("folder watcher: WaitForMultipleObjects");
                }
                DWORD bytes = 0;
                if (signaled == WAIT_OBJECT_0 + 1)
                {
                    // Stopping: cancel the read and wait until Windows no longer writes into the buffer.
                    if (!CancelIoEx(directory.get(), &overlapped) && GetLastError() != ERROR_NOT_FOUND)
                    {
                        ThrowLastError("folder watcher: CancelIoEx");
                    }
                    if (!GetOverlappedResult(directory.get(), &overlapped, &bytes, TRUE) &&
                        GetLastError() != ERROR_OPERATION_ABORTED)
                    {
                        ThrowLastError("folder watcher: GetOverlappedResult (stopping)");
                    }
                    return;
                }
                if (!GetOverlappedResult(directory.get(), &overlapped, &bytes, FALSE))
                {
                    if (GetLastError() == ERROR_NOTIFY_ENUM_DIR)
                    {
                        Log(std::format("folder watcher: changes in {} lost (too many) - read again", ToUtf8(folder.native())));
                        Raise();
                        continue;
                    }
                    ThrowLastError(std::format("watch folder {}", ToUtf8(folder.native())));
                }
                if (bytes == 0)
                {
                    // The buffer overflowed: changes were lost, so everything is read again.
                    Log(std::format("folder watcher: changes in {} lost (buffer full) - read again", ToUtf8(folder.native())));
                    Raise();
                }
                else if (HasShownChange(buffer))
                {
                    Raise();
                }
            }
        }
        catch (...)
        {
            LogException(std::format("folder watcher {} (watch ended)", ToUtf8(folder.native())));
            Raise();
        }
    }
}
