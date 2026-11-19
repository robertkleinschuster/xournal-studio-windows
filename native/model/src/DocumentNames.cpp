#include "xournal/model/DocumentNames.h"

#include <windows.h>

#include <format>

namespace xournal
{
    DateTime DateTime::Now()
    {
        SYSTEMTIME t{};
        GetLocalTime(&t);
        return { t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute };
    }

    namespace DocumentNames
    {
        std::wstring DefaultName(DateTime c)
        {
            return std::format(L"{:04}-{:02}-{:02}-Notiz-{:02}-{:02}", c.year, c.month, c.day, c.hour, c.minute);
        }

        std::wstring NameFor(std::optional<std::filesystem::path> const& file, std::optional<std::filesystem::path> const& pdf,
            DateTime created)
        {
            if (file)
            {
                return file->stem().native();
            }
            if (pdf)
            {
                return pdf->stem().native();
            }
            return DefaultName(created);
        }
    }
}
