#include "AppFolder.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include "inkpages/Log.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <format>
#include <mutex>

namespace xournal::app::AppFolder
{
    std::optional<std::wstring> Environment(wchar_t const* name)
    {
        DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
        if (length == 0)
        {
            DWORD error = GetLastError();
            if (error != ERROR_ENVVAR_NOT_FOUND)
            {
                LogError(std::format("GetEnvironmentVariable {}: {}", ToUtf8(name), DescribeHr(HRESULT_FROM_WIN32(error))));
            }
            return std::nullopt;
        }
        std::wstring value(length, L'\0');
        length = GetEnvironmentVariableW(name, value.data(), length);
        value.resize(length);
        return value;
    }

    std::filesystem::path Path()
    {
        if (auto test = Environment(L"XOURNAL_STUDIO_CONFIG_DIR"))
        {
            std::filesystem::create_directories(*test);
            return *test;
        }
        PWSTR local = nullptr;
        HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local);
        std::wstring folder = SUCCEEDED(hr) ? local : L"";
        CoTaskMemFree(local);
        Check("SHGetKnownFolderPath(LocalAppData)", hr);
        std::filesystem::path path = std::filesystem::path(folder) / L"XournalStudioForWindows";
        std::filesystem::create_directories(path);
        return path;
    }

    std::filesystem::path ErrorLog() { return Path() / L"errors.log"; }

    namespace
    {
        std::mutex errorLock;

        void AppendError(std::string_view line)
        {
            static std::filesystem::path const file = ErrorLog();
            std::scoped_lock guard(errorLock);
            FILE* f = nullptr;
            if (_wfopen_s(&f, file.c_str(), L"ab") != 0 || !f)
            {
                OutputDebugStringA("errors.log cannot be opened; the error was: ");
                OutputDebugStringA(std::string(line).c_str());
                return;
            }
            fwrite(line.data(), 1, line.size(), f);
            fclose(f);
        }
    }

    void SetUpLogging()
    {
        auto log = Environment(L"XOURNAL_STUDIO_LOG").value_or(L"");
        xournal::SetLogFile(log);
        xournal::SetErrorSink(AppendError);
        inkpages::SetLogFile(log);
        inkpages::SetErrorSink(AppendError);
    }
}
