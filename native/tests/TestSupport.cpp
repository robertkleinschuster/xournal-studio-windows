#include "TestSupport.h"

#include "xournal/base/Text.h"
#include "xournal/io/Files.h"
#include "xournal/io/Gzip.h"
#include "xournal/io/XmlText.h"
#include "xournal/model/Strokes.h"

#include <windows.h>
#include <objbase.h>

#include <gtest/gtest.h>

#include <stdexcept>

namespace xournal::tests
{
    namespace
    {
        std::span<std::uint8_t const> Bytes(std::string_view text)
        {
            return { reinterpret_cast<std::uint8_t const*>(text.data()), text.size() };
        }
    }

    XmlElementPtr Xml(std::string_view text)
    {
        // The C# raw strings start at the element; trimmed so a leading line break is no problem for XmlLite.
        while (!text.empty() && (text.front() == '\n' || text.front() == ' ' || text.front() == '\r'))
        {
            text.remove_prefix(1);
        }
        return io::ParseXml(Bytes(text));
    }

    std::filesystem::path WriteFile(std::filesystem::path const& file, std::string_view text, bool gzip)
    {
        while (!text.empty() && (text.front() == '\n' || text.front() == ' '))
        {
            text.remove_prefix(1);
        }
        auto bytes = Bytes(text);
        if (gzip)
        {
            io::Files::WriteReplacing(file, io::Gzip::Compress(bytes));
        }
        else
        {
            io::Files::WriteReplacing(file, bytes);
        }
        return file;
    }

    void WriteBytes(std::filesystem::path const& file, std::string_view bytes)
    {
        io::Files::WriteReplacing(file, Bytes(bytes));
    }

    std::string ReadText(std::filesystem::path const& file)
    {
        auto bytes = io::Files::ReadAll(file);
        return { bytes.begin(), bytes.end() };
    }

    TempFolder::TempFolder()
    {
        GUID guid{};
        if (FAILED(CoCreateGuid(&guid)))
        {
            throw std::runtime_error("CoCreateGuid failed");
        }
        wchar_t text[64];
        StringFromGUID2(guid, text, 64);
        path = std::filesystem::temp_directory_path() / (std::wstring(L"xs-native-tests-") + text);
        std::filesystem::create_directories(path);
    }

    TempFolder::~TempFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
        if (error)
        {
            ADD_FAILURE() << "temp folder left behind: " << error.message();
        }
    }

    XmlElementPtr Normalized(XmlElement const& e)
    {
        auto copy = e.CloneShallow();
        for (auto const& node : e.Nodes())
        {
            if (node.element)
            {
                copy->Append(Normalized(*node.element));
            }
            else if (!((node.kind == XmlNodeKind::Text || node.kind == XmlNodeKind::Whitespace) && Trim(node.text).empty()))
            {
                copy->AppendNode(node);
            }
        }
        return copy;
    }

    StrokeId FakeInk::Add(StrokeInk ink, Matrix transform)
    {
        StrokeId id = next++;
        strokes[id] = { std::move(ink), transform };
        order.push_back(id);
        return id;
    }

    StrokeInk InkOfXml(std::string_view xml)
    {
        auto ink = Strokes::FromXml(*Xml(xml));
        if (!ink)
        {
            throw std::runtime_error("not ink");
        }
        return *ink;
    }
}
