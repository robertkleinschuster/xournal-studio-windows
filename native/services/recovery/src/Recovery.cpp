#include "xournal/recovery/Recovery.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/io/DocumentFile.h"
#include "xournal/io/Files.h"
#include "xournal/project/ProjectFiles.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include <objbase.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <memory>

using namespace winrt::Windows::Data::Json;

namespace xournal::recovery
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

        struct FindCloser
        {
            void operator()(HANDLE h) const
            {
                if (!FindClose(h))
                {
                    LogError(std::format("FindClose: {}", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
                }
            }
        };

        Time FromFileTime(FILETIME const& time)
        {
            auto ticks = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
            return Time(Time::duration(static_cast<Time::rep>(ticks)));
        }

        // 64-bit times as decimal strings: JSON numbers are doubles and would round them.
        std::wstring TimeText(Time time)
        {
            return std::to_wstring(time.time_since_epoch().count());
        }

        std::optional<Time> ParseTime(std::wstring_view text)
        {
            std::string digits = ToUtf8(text);
            Time::rep value = 0;
            auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
            if (error != std::errc{} || end != digits.data() + digits.size())
            {
                return std::nullopt;
            }
            return Time(Time::duration(value));
        }

        std::wstring DateTimeText(DateTime const& t)
        {
            return std::format(L"{:04}-{:02}-{:02}T{:02}:{:02}", t.year, t.month, t.day, t.hour, t.minute);
        }

        std::optional<DateTime> ParseDateTime(std::wstring_view text)
        {
            DateTime t;
            if (swscanf_s(std::wstring(text).c_str(), L"%d-%d-%dT%d:%d", &t.year, &t.month, &t.day, &t.hour, &t.minute) != 5)
            {
                return std::nullopt;
            }
            return t;
        }

        std::optional<std::wstring> String(JsonObject const& o, wchar_t const* key)
        {
            if (!o.HasKey(key) || o.GetNamedValue(key).ValueType() != JsonValueType::String)
            {
                return std::nullopt;
            }
            return std::wstring(o.GetNamedString(key));
        }

        std::string InfoJson(RecoveryInfo const& info)
        {
            JsonObject o;
            auto setString = [&](wchar_t const* key, std::optional<std::wstring> const& value)
            {
                o.SetNamedValue(key, value ? JsonValue::CreateStringValue(*value) : JsonValue::CreateNullValue());
            };
            setString(L"documentPath", info.documentPath ? std::optional(info.documentPath->native()) : std::nullopt);
            setString(L"referenceBase", info.referenceBase.native());
            setString(L"pdfPath", info.pdfPath ? std::optional(info.pdfPath->native()) : std::nullopt);
            setString(L"createdAt", info.createdAt ? std::optional(DateTimeText(*info.createdAt)) : std::nullopt);
            setString(L"savedAt", TimeText(info.savedAt));
            o.SetNamedValue(L"processId", JsonValue::CreateNumberValue(info.process.id));
            setString(L"processStart", TimeText(info.process.start));
            return ToUtf8(o.Stringify());
        }

        // Nothing when the file is not a complete info (it is then logged by the caller and left alone).
        std::optional<RecoveryInfo> ParseInfo(std::wstring const& text)
        {
            JsonObject o{ nullptr };
            if (!JsonObject::TryParse(text, o))
            {
                return std::nullopt;
            }
            auto referenceBase = String(o, L"referenceBase");
            auto savedAt = String(o, L"savedAt");
            auto processStart = String(o, L"processStart");
            if (!referenceBase || !savedAt || !processStart || !o.HasKey(L"processId") ||
                o.GetNamedValue(L"processId").ValueType() != JsonValueType::Number)
            {
                return std::nullopt;
            }
            RecoveryInfo info;
            info.referenceBase = *referenceBase;
            if (auto document = String(o, L"documentPath")) info.documentPath = *document;
            if (auto pdf = String(o, L"pdfPath")) info.pdfPath = *pdf;
            if (auto created = String(o, L"createdAt")) info.createdAt = ParseDateTime(*created);
            auto saved = ParseTime(*savedAt);
            auto start = ParseTime(*processStart);
            if (!saved || !start)
            {
                return std::nullopt;
            }
            info.savedAt = *saved;
            info.process = { static_cast<DWORD>(o.GetNamedNumber(L"processId")), *start };
            return info;
        }

        // A file this module wrote is removed; one that is not there is fine.
        void DeleteIfThere(path const& file)
        {
            if (!DeleteFileW(file.c_str()))
            {
                DWORD error = GetLastError();
                if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                {
                    ThrowHr(std::format("delete {}", ToUtf8(file.native())), HRESULT_FROM_WIN32(error));
                }
            }
        }

        // "2026-11-19 14-05" in local time (a file name cannot hold ':').
        std::wstring LocalMinuteText(Time time)
        {
            auto ticks = static_cast<std::uint64_t>(time.time_since_epoch().count());
            FILETIME utc{ static_cast<DWORD>(ticks & 0xFFFFFFFF), static_cast<DWORD>(ticks >> 32) };
            SYSTEMTIME utcParts{}, local{};
            if (!FileTimeToSystemTime(&utc, &utcParts))
            {
                ThrowLastError("FileTimeToSystemTime");
            }
            if (!SystemTimeToTzSpecificLocalTime(nullptr, &utcParts, &local))
            {
                ThrowLastError("SystemTimeToTzSpecificLocalTime");
            }
            return std::format(L"{:04}-{:02}-{:02} {:02}-{:02}", local.wYear, local.wMonth, local.wDay, local.wHour,
                local.wMinute);
        }
    }

    ProcessStamp ProcessStamp::Current()
    {
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        {
            ThrowLastError("GetProcessTimes (this process)");
        }
        return { GetCurrentProcessId(), FromFileTime(creation) };
    }

    path Recovery::FolderIn(path const& appFolder)
    {
        return appFolder / L"Recovery-native";
    }

    std::wstring Recovery::NewId()
    {
        GUID guid{};
        Check("CoCreateGuid", CoCreateGuid(&guid));
        return std::format(L"{:08x}{:04x}{:04x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", guid.Data1, guid.Data2,
            guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
            guid.Data4[6], guid.Data4[7]);
    }

    void Recovery::Write(std::wstring const& id, XmlElement const& snapshot, RecoveryInfo const& info) const
    {
        if (std::error_code error; std::filesystem::create_directories(folder, error), error)
        {
            ThrowHr(std::format("create folder {}", ToUtf8(folder.native())), HRESULT_FROM_WIN32(error.value()));
        }
        io::DocumentFile::Write(snapshot, CopyPath(id));
        std::string json = InfoJson(info);
        io::Files::WriteReplacing(InfoPath(id), { reinterpret_cast<std::uint8_t const*>(json.data()), json.size() });
    }

    void Recovery::Delete(std::wstring const& id) const
    {
        DeleteIfThere(InfoPath(id));
        DeleteIfThere(CopyPath(id));
        DeleteIfThere(CopyPath(id).native() + L"~");
    }

    void Recovery::Recycle(RecoveryEntry const& entry, std::wstring const& name, HWND owner) const
    {
        std::wstring readable = std::format(L"{} (Wiederherstellung {})", name, LocalMinuteText(entry.info.savedAt));
        std::ranges::replace_if(readable, [](wchar_t c)
        {
            return c < 32 || std::wstring_view(L"\\/:*?\"<>|").find(c) != std::wstring_view::npos;
        }, L'_');
        path renamed = project::ProjectFiles::Rename(entry.copyPath,
            project::ProjectFiles::UniqueName(entry.copyPath.parent_path(), readable, L".xopp"));
        try
        {
            project::ProjectFiles::Recycle(renamed, owner);
        }
        catch (...)
        {
            // Not recycled: back to the name its info refers to, so it is offered again.
            LogException(std::format("recycle recovery copy {}", ToUtf8(entry.id)));
            try
            {
                project::ProjectFiles::Rename(renamed, entry.copyPath.filename().native());
            }
            catch (...)
            {
                LogException(std::format("rename recovery copy {} back (it stays as {})", ToUtf8(entry.id),
                    ToUtf8(renamed.native())));
            }
            throw;
        }
        DeleteIfThere(InfoPath(entry.id));
    }

    std::vector<RecoveryEntry> Recovery::FindAbandoned() const
    {
        std::vector<RecoveryEntry> entries;
        std::wstring pattern = (folder / L"*.json").native();
        WIN32_FIND_DATAW data{};
        HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
        if (h == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            {
                return entries; // nothing was ever written
            }
            ThrowHr(std::format("list {}", ToUtf8(folder.native())), HRESULT_FROM_WIN32(error));
        }
        std::unique_ptr<void, FindCloser> find(h);
        std::vector<std::wstring> names;
        do
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                names.emplace_back(data.cFileName);
            }
        } while (FindNextFileW(h, &data));
        if (DWORD error = GetLastError(); error != ERROR_NO_MORE_FILES)
        {
            ThrowHr(std::format("list {}", ToUtf8(folder.native())), HRESULT_FROM_WIN32(error));
        }
        for (std::wstring const& name : names)
        {
            path infoPath = folder / name;
            std::wstring id = infoPath.stem().native();
            std::optional<RecoveryInfo> info;
            try
            {
                auto bytes = io::Files::ReadAll(infoPath);
                info = ParseInfo(FromUtf8(std::string_view(reinterpret_cast<char const*>(bytes.data()), bytes.size())));
            }
            catch (Error const&)
            {
                LogException(std::format("recovery info {} unreadable (not offered, kept)", ToUtf8(infoPath.native())));
                continue;
            }
            if (!info)
            {
                LogError(std::format("recovery info {} is not a recovery info (not offered, kept)", ToUtf8(infoPath.native())));
                continue;
            }
            if (Paths::FileExists(CopyPath(id)) && !IsRunning(info->process))
            {
                entries.push_back({ id, std::move(*info), CopyPath(id) });
            }
        }
        std::ranges::sort(entries, [](RecoveryEntry const& a, RecoveryEntry const& b) { return a.info.savedAt > b.info.savedAt; });
        return entries;
    }

    bool Recovery::IsRunning(ProcessStamp const& process)
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.id);
        if (!h)
        {
            DWORD error = GetLastError();
            // ERROR_INVALID_PARAMETER: no process with this id. Anything else (access denied): not ours to inspect,
            // so not this app's session either.
            Log(std::format("recovery: process {} not running or not inspectable ({}) - session ended", process.id,
                DescribeHr(HRESULT_FROM_WIN32(error))));
            return false;
        }
        std::unique_ptr<void, HandleCloser> handle(h);
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(h, &exitCode))
        {
            ThrowLastError(std::format("GetExitCodeProcess {}", process.id));
        }
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(h, &creation, &exit, &kernel, &user))
        {
            ThrowLastError(std::format("GetProcessTimes {}", process.id));
        }
        // An exited process can still be opened while handles to it exist; STILL_ACTIVE is then no longer its code
        // (a process that really exits with 259 would count as running - the documented caveat, harmless here).
        return exitCode == STILL_ACTIVE && FromFileTime(creation) == process.start;
    }
}
