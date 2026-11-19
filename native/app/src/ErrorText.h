#pragma once

// The text of an error for a message to the user (the log has its type and HRESULT already).

#include <exception>
#include <string>

namespace xournal::app
{
    std::wstring ErrorText(std::exception_ptr error = std::current_exception());
}
