#include "ErrorText.h"

#include "xournal/base/Text.h"

#include <winrt/base.h>

namespace xournal::app
{
    std::wstring ErrorText(std::exception_ptr error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (winrt::hresult_error const& e)
        {
            return std::wstring(e.message());
        }
        catch (std::exception const& e)
        {
            return FromUtf8(e.what());
        }
        catch (...)
        {
            return L"unbekannter Fehler";
        }
    }
}
