#include "xournal/io/Files.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <windows.h>

#include <algorithm>
#include <format>
#include <memory>

namespace xournal::io::Files
{
    namespace
    {
        struct HandleCloser
        {
            void operator()(HANDLE h) const
            {
                if (!CloseHandle(h))
                {
                    LogError(std::format("CloseHandle: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
                }
            }
        };
        using File = std::unique_ptr<void, HandleCloser>;

        std::string Name(std::filesystem::path const& p) { return ToUtf8(p.native()); }

        File Open(std::filesystem::path const& p, DWORD access, DWORD creation)
        {
            HANDLE h = CreateFileW(p.c_str(), access, FILE_SHARE_READ, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
            {
                ThrowLastError(std::format("open {}", Name(p)));
            }
            return File(h);
        }

        void WriteAll(std::filesystem::path const& p, std::span<std::uint8_t const> data)
        {
            File file = Open(p, GENERIC_WRITE, CREATE_ALWAYS);
            size_t done = 0;
            while (done < data.size())
            {
                DWORD part = static_cast<DWORD>(std::min<size_t>(data.size() - done, 1u << 30));
                DWORD written = 0;
                if (!WriteFile(file.get(), data.data() + done, part, &written, nullptr))
                {
                    ThrowLastError(std::format("write {}", Name(p)));
                }
                done += written;
            }
            if (!FlushFileBuffers(file.get()))
            {
                ThrowLastError(std::format("flush {}", Name(p)));
            }
        }
    }

    std::vector<std::uint8_t> ReadAll(std::filesystem::path const& p)
    {
        File file = Open(p, GENERIC_READ, OPEN_EXISTING);
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size))
        {
            ThrowLastError(std::format("size of {}", Name(p)));
        }
        std::vector<std::uint8_t> data(static_cast<size_t>(size.QuadPart));
        size_t done = 0;
        while (done < data.size())
        {
            DWORD part = static_cast<DWORD>(std::min<size_t>(data.size() - done, 1u << 30));
            DWORD read = 0;
            if (!ReadFile(file.get(), data.data() + done, part, &read, nullptr))
            {
                ThrowLastError(std::format("read {}", Name(p)));
            }
            if (read == 0)
            {
                data.resize(done); // the file got shorter while reading
                break;
            }
            done += read;
        }
        return data;
    }

    void WriteReplacing(std::filesystem::path const& p, std::span<std::uint8_t const> data)
    {
        std::filesystem::path temp = p.native() + L"~";
        try
        {
            WriteAll(temp, data);
            if (!MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                ThrowLastError(std::format("replace {} with {}", Name(p), Name(temp)));
            }
        }
        catch (Error const& e)
        {
            // Only the half-written file of this write is removed; the document itself is untouched.
            if (!DeleteFileW(temp.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
            {
                HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
                std::string message = std::format("{}; {} was left behind: {}", e.what(), Name(temp), DescribeHr(hr));
                LogError(message);
                throw Error(message, e.Code());
            }
            throw;
        }
    }

    void CopyNew(std::filesystem::path const& from, std::filesystem::path const& to)
    {
        if (!CopyFileW(from.c_str(), to.c_str(), TRUE))
        {
            ThrowLastError(std::format("copy {} to {}", Name(from), Name(to)));
        }
    }

    void Delete(std::filesystem::path const& p)
    {
        if (!DeleteFileW(p.c_str()))
        {
            ThrowLastError(std::format("delete {}", Name(p)));
        }
    }
}
