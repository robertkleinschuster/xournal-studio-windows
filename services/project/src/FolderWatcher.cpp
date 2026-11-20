#include "xournal/project/FolderWatcher.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"

#include <format>

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
        : folder(Paths::Full(folderToWatch)), changed(std::move(onChanged)), buffer(BufferSize / sizeof(DWORD))
    {
        HANDLE dir = CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (dir == INVALID_HANDLE_VALUE)
        {
            ThrowLastError(std::format("watch folder {}", ToUtf8(folder.native())));
        }
        directory.reset(dir);
        io = CreateThreadpoolIo(directory.get(), Completed, this, nullptr);
        if (!io) ThrowLastError("CreateThreadpoolIo (folder watcher)");
        try
        {
            Read();
        }
        catch (...)
        {
            directory.reset();
            CloseThreadpoolIo(io);
            throw;
        }
    }

    FolderWatcher::~FolderWatcher()
    {
        {
            std::scoped_lock guard(lock);
            stopping = true;
            if (!CancelIoEx(directory.get(), &overlapped))
            {
                DWORD error = GetLastError();
                if (error != ERROR_NOT_FOUND)
                    LogError(std::format("CancelIoEx (folder watcher): {}", DescribeHr(HRESULT_FROM_WIN32(error))));
            }
        }
        WaitForThreadpoolIoCallbacks(io, FALSE);
        directory.reset();
        CloseThreadpoolIo(io);
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

    bool FolderWatcher::HasShownChange() const
    {
        auto const* records = reinterpret_cast<std::uint8_t const*>(buffer.data());
        for (DWORD offset = 0;;)
        {
            auto const* info = reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(records + offset);
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

    void FolderWatcher::Read()
    {
        std::scoped_lock guard(lock);
        if (stopping) return;
        overlapped = {};
        StartThreadpoolIo(io);
        if (!ReadDirectoryChangesW(directory.get(), buffer.data(), BufferSize, TRUE, Filter, nullptr, &overlapped, nullptr))
        {
            DWORD error = GetLastError();
            CancelThreadpoolIo(io); // no completion will arrive for the failed submission
            ThrowHr(std::format("watch folder {}", ToUtf8(folder.native())), HRESULT_FROM_WIN32(error));
        }
    }

    void CALLBACK FolderWatcher::Completed(PTP_CALLBACK_INSTANCE, void* context, void*, ULONG result, ULONG_PTR bytes, PTP_IO) noexcept
    {
        static_cast<FolderWatcher*>(context)->OnCompleted(result, bytes);
    }

    void FolderWatcher::OnCompleted(ULONG result, ULONG_PTR bytes) noexcept
    {
        try
        {
            {
                std::scoped_lock guard(lock);
                if (stopping) return;
            }
            if (result != ERROR_SUCCESS && result != ERROR_NOTIFY_ENUM_DIR)
                ThrowHr(std::format("watch folder {}", ToUtf8(folder.native())), HRESULT_FROM_WIN32(result));
            if (result == ERROR_NOTIFY_ENUM_DIR || bytes == 0)
            {
                Log(std::format("folder watcher: changes in {} lost - read again", ToUtf8(folder.native())));
                Raise();
            }
            else if (HasShownChange())
                Raise();
            Read();
        }
        catch (...)
        {
            LogException(std::format("folder watcher {} (watch ended)", ToUtf8(folder.native())));
            Raise();
        }
    }
}
