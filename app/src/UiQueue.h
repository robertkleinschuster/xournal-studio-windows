#pragma once

// Work for the UI thread from other threads through the Windows DispatcherQueue. Exceptions of the work are logged.

#include <windows.h>

#include <functional>
#include <string>

namespace xournal::app::UiQueue
{
    /// Open the main window's dispatch gate on the UI thread.
    void Attach();
    void Detach();

    /// Queues work for the UI thread (any thread). `name` goes into the log if the work fails.
    bool Post(std::string name, std::function<void()> work);

    /// Runs work on the Windows thread pool (TrySubmitThreadpoolCallback); exceptions are logged.
    void RunInBackground(std::string name, std::function<void()> work);
}
