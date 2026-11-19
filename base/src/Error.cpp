#include "xournal/base/Error.h"

#include "xournal/base/Log.h"

#include <format>

namespace xournal
{
    Error::Error(std::string const& message, HRESULT hr) : std::runtime_error(message), code(hr) {}

    void ThrowHr(std::string_view step, HRESULT hr)
    {
        std::string message = std::format("{}: {}", step, DescribeHr(hr));
        LogError(message);
        throw Error(message, hr);
    }

    void ThrowLastError(std::string_view step)
    {
        ThrowHr(step, HRESULT_FROM_WIN32(GetLastError()));
    }

    void ThrowInvalidData(std::string_view what)
    {
        HRESULT hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        std::string message = std::format("{} (0x{:08X})", what, static_cast<unsigned>(hr));
        LogError(message);
        throw Error(message, hr);
    }

    HRESULT Check(std::string_view step, HRESULT hr)
    {
        if (FAILED(hr))
        {
            ThrowHr(step, hr);
        }
        return hr;
    }
}
