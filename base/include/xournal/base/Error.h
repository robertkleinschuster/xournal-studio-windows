#pragma once

// Errors carry their HRESULT and the system's text, so whoever catches one can log what really happened (rule: never
// swallow an error). Throwing logs the error at once as well: a caller that only shows a message still leaves the cause
// in the log.

#include <windows.h>

#include <stdexcept>
#include <string>
#include <string_view>

namespace xournal
{
    class Error : public std::runtime_error
    {
    public:
        /// `message`: UTF-8, e.g. "write C:\x.xopp~: 0x80070005 Zugriff verweigert".
        Error(std::string const& message, HRESULT hr);

        HRESULT Code() const noexcept { return code; }

    private:
        HRESULT code;
    };

    /// Logs and throws: "<step>: 0x80070005 <system text>".
    [[noreturn]] void ThrowHr(std::string_view step, HRESULT hr);

    /// ThrowHr with GetLastError() of the call that just failed.
    [[noreturn]] void ThrowLastError(std::string_view step);

    /// Logs and throws a content error (a file that is not what it claims to be): HRESULT_FROM_WIN32(ERROR_INVALID_DATA).
    [[noreturn]] void ThrowInvalidData(std::string_view what);

    /// Throws on a failed HRESULT (see ThrowHr); returns hr otherwise.
    HRESULT Check(std::string_view step, HRESULT hr);
}
