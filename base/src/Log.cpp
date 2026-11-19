#include "xournal/base/Log.h"

#include "xournal/base/Error.h"
#include "xournal/base/Text.h"

#include <chrono>
#include <cstdio>
#include <format>
#include <mutex>
#include <typeinfo>

namespace xournal
{
    namespace
    {
        std::mutex lock;
        std::wstring file;
        std::function<void(std::string_view)> errorSink;

        // FILE_APPEND_DATA: Windows appends each write at the end atomically, also against other writers of the file
        // (the CRT's "ab" seeks, then writes - two writers lost lines that way, 2026-11-19).
        void AppendToFile(std::string const& line)
        {
            HANDLE f = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE)
            {
                // The log itself cannot fail loudly (it is where failures go): the debugger still sees both lines.
                OutputDebugStringA(std::format("xournal log: cannot open the log file ({})\n", GetLastError()).c_str());
                OutputDebugStringA(line.c_str());
                return;
            }
            DWORD written = 0;
            if (!WriteFile(f, line.data(), static_cast<DWORD>(line.size()), &written, nullptr))
            {
                OutputDebugStringA(std::format("xournal log: write failed ({})\n", GetLastError()).c_str());
                OutputDebugStringA(line.c_str());
            }
            CloseHandle(f);
        }

        void Write(std::string_view text, bool error)
        {
            auto local = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
            std::string line = std::format("{:%H:%M:%S} {} {}{}\n",
                std::chrono::floor<std::chrono::milliseconds>(local), GetCurrentThreadId(), error ? "ERROR " : "", text);
            std::function<void(std::string_view)> sink;
            {
                std::scoped_lock guard(lock);
                if (!file.empty())
                {
                    AppendToFile(line);
                }
                sink = errorSink;
            }
            if (error && sink)
            {
                sink(line);
            }
        }
    }

    void SetLogFile(std::wstring path)
    {
        std::scoped_lock guard(lock);
        file = std::move(path);
    }

    void SetErrorSink(std::function<void(std::string_view line)> sink)
    {
        std::scoped_lock guard(lock);
        errorSink = std::move(sink);
    }

    void Log(std::string_view text) { Write(text, false); }

    void LogError(std::string_view text) { Write(text, true); }

    std::string DescribeHr(HRESULT hr)
    {
        wchar_t* buffer = nullptr;
        DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::wstring message;
        if (length > 0 && buffer)
        {
            message.assign(buffer, length);
            LocalFree(buffer);
        }
        std::string text = ToUtf8(Trim(message));
        return text.empty() ? std::format("0x{:08X}", static_cast<unsigned>(hr))
                            : std::format("0x{:08X} {}", static_cast<unsigned>(hr), text);
    }

    void LogException(std::string_view context, std::exception_ptr error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (Error const& e)
        {
            LogError(std::format("{}: xournal::Error {}", context, e.what()));
        }
        catch (std::exception const& e)
        {
            LogError(std::format("{}: {} {}", context, typeid(e).name(), e.what()));
        }
        catch (...)
        {
            LogError(std::format("{}: unknown exception", context));
        }
    }
}
