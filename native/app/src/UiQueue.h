#pragma once

// Work for the UI thread from other threads (a document loaded on the thread pool, the ink thread's notices): posted to
// the main window as a message, run there in order. Exceptions of the work are logged.

#include <windows.h>

#include <functional>
#include <string>

namespace xournal::app::UiQueue
{
    /// The window whose message loop runs the work, and the message it uses.
    void Attach(HWND window, UINT message);

    /// Queues work for the UI thread (any thread). `name` goes into the log if the work fails.
    void Post(std::string name, std::function<void()> work);

    /// Runs one posted item (the window's procedure calls this for the message).
    void Run(LPARAM item);

    /// Runs work on the Windows thread pool (TrySubmitThreadpoolCallback); exceptions are logged.
    void RunInBackground(std::string name, std::function<void()> work);
}
