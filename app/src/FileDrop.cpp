#include "FileDrop.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <format>
#include <string>

namespace xournal::app::FileDrop
{
    std::vector<std::filesystem::path> Documents(HDROP drop, size_t& others)
    {
        struct Finish
        {
            HDROP drop;
            ~Finish() { DragFinish(drop); }
        } finish{ drop };
        others = 0;
        std::vector<std::filesystem::path> documents;
        UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i)
        {
            UINT length = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring name(length + 1, L'\0');
            if (length == 0 || DragQueryFileW(drop, i, name.data(), length + 1) != length)
            {
                LogError(std::format("dropped file {} of {} could not be read", i + 1, count));
                ++others;
                continue;
            }
            name.resize(length);
            std::filesystem::path file(name);
            std::wstring extension = file.extension().native();
            bool document = false;
            for (wchar_t const* known : { L".xopp", L".moj", L".pdf" }) document = document || EqualsIgnoreCase(extension, known);
            if (document) documents.push_back(std::move(file));
            else ++others;
        }
        Log(std::format("files dropped: {} documents, {} others", documents.size(), others));
        return documents;
    }
}
