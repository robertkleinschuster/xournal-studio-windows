#pragma once

// Where the app keeps its files: %LOCALAPPDATA%\XournalStudioForWindows - the C# app's folder, so both find the same
// recovery copies later. The native app writes only its own files there (settings-native.json, errors.log, the log),
// never the C# app's settings.json.
// Test hooks (as the C# app): XOURNAL_STUDIO_CONFIG_DIR (another folder), XOURNAL_STUDIO_LOG (a log file; InkPages'
// lines go to "<log>.inkpages.log" beside it).

#include <filesystem>
#include <optional>
#include <string>

namespace xournal::app::AppFolder
{
    std::filesystem::path Path();

    /// errors.log there: every error line is appended (always on).
    std::filesystem::path ErrorLog();

    /// An environment variable, or nothing when it is not set.
    std::optional<std::wstring> Environment(wchar_t const* name);

    /// Sets up the log (XOURNAL_STUDIO_LOG), errors.log, and routes InkPages' log to the same places.
    void SetUpLogging();
}
