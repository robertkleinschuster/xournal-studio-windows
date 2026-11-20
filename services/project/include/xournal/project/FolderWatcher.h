#pragma once

// Reports changes in the project folder (files and folders created, deleted, renamed or written) with
// ReadDirectoryChangesW with Windows thread-pool I/O. Changes inside hidden folders such as .git are left out (git
// status itself writes there). `changed` runs on a pool thread, at least once per batch of changes Windows reports:
// the receiver debounces and reads the folder again. An exception from `changed` is logged, never let out of the thread.
// When Windows loses changes (buffer overflow, e.g. a checkout) `changed` runs too, so everything is read again; when
// watching fails for good (the folder was deleted) the error is logged, `changed` runs once more and the watch ends.

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace xournal::project
{
    class FolderWatcher
    {
    public:
        /// Starts watching at once; throws when the folder cannot be opened.
        FolderWatcher(std::filesystem::path folder, std::function<void()> changed);
        /// Cancels the read and waits for completion callbacks (a running `changed` finishes first).
        ~FolderWatcher();
        FolderWatcher(FolderWatcher const&) = delete;
        FolderWatcher& operator=(FolderWatcher const&) = delete;

        std::filesystem::path const& Folder() const { return folder; }

        /// True for paths inside a folder whose name starts with a dot (.git, ...), and for such a folder itself.
        static bool IsInHiddenFolder(std::filesystem::path const& folder, std::filesystem::path const& p);

    private:
        struct HandleCloser
        {
            void operator()(HANDLE h) const;
        };
        using Handle = std::unique_ptr<void, HandleCloser>;

        static void CALLBACK Completed(PTP_CALLBACK_INSTANCE, void* context, void*, ULONG result, ULONG_PTR bytes, PTP_IO) noexcept;
        void Read();
        void OnCompleted(ULONG result, ULONG_PTR bytes) noexcept;
        void Raise() noexcept;
        bool HasShownChange() const;

        std::filesystem::path folder;
        std::function<void()> changed;
        Handle directory;
        std::vector<DWORD> buffer;
        OVERLAPPED overlapped{};
        PTP_IO io = nullptr;
        std::mutex lock;
        bool stopping = false;
    };
}
