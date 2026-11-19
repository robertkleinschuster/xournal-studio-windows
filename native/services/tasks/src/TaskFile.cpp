#include "xournal/tasks/TaskFile.h"

#include "xournal/base/Error.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <span>

namespace xournal::tasks
{
    namespace
    {
        enum class Encoding
        {
            Utf8,
            Utf8Bom,
            Utf16Le,
            Utf16Be,
        };

        struct Content
        {
            std::vector<std::wstring> lines;
            std::wstring newLine = L"\n";
            Encoding encoding = Encoding::Utf8;
            bool finalNewline = true;
        };

        bool StartsWith(std::span<std::uint8_t const> bytes, std::initializer_list<std::uint8_t> prefix)
        {
            return bytes.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), bytes.begin());
        }

        std::wstring Utf16(std::span<std::uint8_t const> bytes, bool bigEndian, std::filesystem::path const& file)
        {
            if (bytes.size() % 2 != 0)
            {
                ThrowInvalidData(std::format("{} ist kein vollständiges UTF-16; unverändert gelassen.", ToUtf8(file.native())));
            }
            std::wstring text(bytes.size() / 2, L'\0');
            for (size_t i = 0; i < text.size(); i++)
            {
                std::uint8_t first = bytes[2 * i], second = bytes[2 * i + 1];
                text[i] = static_cast<wchar_t>(bigEndian ? (first << 8) | second : (second << 8) | first);
            }
            return text;
        }

        // Strict UTF-8: a byte sequence that is not UTF-8 is refused (MB_ERR_INVALID_CHARS), never guessed.
        std::wstring StrictUtf8(std::span<std::uint8_t const> bytes, std::filesystem::path const& file)
        {
            if (bytes.empty())
            {
                return {};
            }
            auto const* data = reinterpret_cast<char const*>(bytes.data());
            int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(bytes.size()), nullptr, 0);
            if (length <= 0)
            {
                if (GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
                {
                    ThrowInvalidData(std::format("{} ist nicht in UTF-8 gespeichert (todo.txt-Format); unverändert gelassen.",
                        ToUtf8(file.native())));
                }
                ThrowLastError(std::format("read {} as UTF-8", ToUtf8(file.native())));
            }
            std::wstring text(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, static_cast<int>(bytes.size()), text.data(), length);
            return text;
        }

        Content Read(std::filesystem::path const& file)
        {
            std::vector<std::uint8_t> data = io::Files::ReadAll(file);
            std::span<std::uint8_t const> bytes(data);
            Content content;
            std::wstring text;
            if (StartsWith(bytes, { 0xFF, 0xFE, 0x00, 0x00 }) || StartsWith(bytes, { 0x00, 0x00, 0xFE, 0xFF }))
            {
                ThrowInvalidData(std::format("{} ist in UTF-32 gespeichert (todo.txt: UTF-8); unverändert gelassen.",
                    ToUtf8(file.native())));
            }
            if (StartsWith(bytes, { 0xEF, 0xBB, 0xBF }))
            {
                content.encoding = Encoding::Utf8Bom;
                text = StrictUtf8(bytes.subspan(3), file);
            }
            else if (StartsWith(bytes, { 0xFF, 0xFE }))
            {
                content.encoding = Encoding::Utf16Le;
                text = Utf16(bytes.subspan(2), false, file);
            }
            else if (StartsWith(bytes, { 0xFE, 0xFF }))
            {
                content.encoding = Encoding::Utf16Be;
                text = Utf16(bytes.subspan(2), true, file);
            }
            else
            {
                text = StrictUtf8(bytes, file);
            }
            if (text.empty())
            {
                return content; // no lines, a final newline once something is added
            }
            content.newLine = text.find(L"\r\n") != std::wstring::npos ? L"\r\n" : L"\n";
            content.finalNewline = text.back() == L'\n';
            size_t start = 0;
            for (;;)
            {
                size_t end = text.find(L'\n', start);
                std::wstring line = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
                if (!line.empty() && line.back() == L'\r')
                {
                    line.pop_back();
                }
                if (end == std::wstring::npos)
                {
                    if (!content.finalNewline)
                    {
                        content.lines.push_back(std::move(line));
                    }
                    break;
                }
                content.lines.push_back(std::move(line));
                start = end + 1;
            }
            return content;
        }

        std::vector<std::uint8_t> Encode(std::wstring const& text, Encoding encoding)
        {
            std::vector<std::uint8_t> bytes;
            switch (encoding)
            {
            case Encoding::Utf8Bom:
                bytes = { 0xEF, 0xBB, 0xBF };
                [[fallthrough]];
            case Encoding::Utf8:
            {
                std::string utf8 = ToUtf8(text);
                bytes.insert(bytes.end(), utf8.begin(), utf8.end());
                break;
            }
            case Encoding::Utf16Le:
            case Encoding::Utf16Be:
            {
                bool bigEndian = encoding == Encoding::Utf16Be;
                bytes = bigEndian ? std::vector<std::uint8_t>{ 0xFE, 0xFF } : std::vector<std::uint8_t>{ 0xFF, 0xFE };
                for (wchar_t c : text)
                {
                    auto high = static_cast<std::uint8_t>(c >> 8), low = static_cast<std::uint8_t>(c & 0xFF);
                    bytes.push_back(bigEndian ? high : low);
                    bytes.push_back(bigEndian ? low : high);
                }
                break;
            }
            }
            return bytes;
        }

        bool Change(std::filesystem::path const& file, std::function<bool(std::vector<std::wstring>&)> const& change)
        {
            Content content = Paths::FileExists(file) ? Read(file) : Content{};
            if (!change(content.lines))
            {
                return false;
            }
            std::wstring text;
            for (size_t i = 0; i < content.lines.size(); i++)
            {
                text += content.lines[i];
                if (i + 1 < content.lines.size() || content.finalNewline)
                {
                    text += content.newLine;
                }
            }
            io::Files::WriteReplacing(file, Encode(text, content.encoding));
            return true;
        }
    }

    bool TaskFile::Exists() const
    {
        return Paths::FileExists(file);
    }

    std::vector<std::wstring> TaskFile::ReadLines() const
    {
        return Exists() ? Read(file).lines : std::vector<std::wstring>{};
    }

    bool TaskFile::Replace(std::wstring_view oldLine, std::wstring_view newLine)
    {
        return Change(file, [&](std::vector<std::wstring>& lines)
        {
            auto it = std::ranges::find(lines, oldLine);
            if (it == lines.end())
            {
                return false;
            }
            *it = newLine;
            return true;
        });
    }

    bool TaskFile::Remove(std::wstring_view line)
    {
        return Change(file, [&](std::vector<std::wstring>& lines)
        {
            auto it = std::ranges::find(lines, line);
            if (it == lines.end())
            {
                return false;
            }
            lines.erase(it);
            return true;
        });
    }

    void TaskFile::Append(std::wstring_view line)
    {
        Change(file, [&](std::vector<std::wstring>& lines)
        {
            lines.emplace_back(line);
            return true;
        });
    }
}
