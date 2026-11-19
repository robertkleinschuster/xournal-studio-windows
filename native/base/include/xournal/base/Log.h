#pragma once

// The app's log: one line per call with time and thread. The app chooses where lines go; errors also go to an error
// sink (e.g. an errors.log that is always written). Same contract as inkpages::Log, so the app can route both alike.

#include <windows.h>

#include <exception>
#include <functional>
#include <string>
#include <string_view>

namespace xournal
{
    /// Lines go to this file (UTF-8, appended); empty: nowhere (the default).
    void SetLogFile(std::wstring path);

    /// Errors are also handed to this callback (null: none).
    void SetErrorSink(std::function<void(std::string_view line)> sink);

    void Log(std::string_view text);
    void LogError(std::string_view text);

    /// "0x80070005 Zugriff verweigert" - the HRESULT and the system's text for it.
    std::string DescribeHr(HRESULT hr);

    /// Logs the exception in flight (call inside a catch) with its type, HRESULT (xournal::Error) and message.
    void LogException(std::string_view context, std::exception_ptr error = std::current_exception());
}
