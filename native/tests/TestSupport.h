#pragma once

// Helpers the tests share: XML from strings, a temporary folder, files, and an in-memory ink layer.

#include "xournal/model/Ink.h"
#include "xournal/model/Xml.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace xournal::tests
{
    /// The element of an XML text (UTF-8 source, as the C# tests' raw strings).
    XmlElementPtr Xml(std::string_view text);

    /// Writes text to a file, gzip-compressed or plain.
    std::filesystem::path WriteFile(std::filesystem::path const& file, std::string_view text, bool gzip = true);
    void WriteBytes(std::filesystem::path const& file, std::string_view bytes);
    std::string ReadText(std::filesystem::path const& file);

    /// A fresh folder below %TEMP%, removed when the object goes.
    class TempFolder
    {
    public:
        TempFolder();
        ~TempFolder();
        TempFolder(TempFolder const&) = delete;
        TempFolder& operator=(TempFolder const&) = delete;
        std::filesystem::path const& Path() const { return path; }
        std::filesystem::path operator/(std::wstring_view name) const { return path / name; }

    private:
        std::filesystem::path path;
    };

    /// Whitespace-only text nodes removed (they depend on formatting, not content), recursively, on a copy.
    XmlElementPtr Normalized(XmlElement const& e);

    /// An ink layer in memory: strokes in drawing order with their transforms.
    class FakeInk : public InkView
    {
    public:
        StrokeId Add(StrokeInk ink, Matrix transform = {});
        void SetTransform(StrokeId id, Matrix transform) { strokes.at(id).transform = transform; }
        void SetOrder(std::vector<StrokeId> ids) { order = std::move(ids); }

        std::vector<StrokeId> Strokes() const override { return order; }
        Matrix TransformOf(StrokeId id) const override { return strokes.at(id).transform; }
        StrokeInk InkOf(StrokeId id) const override { return strokes.at(id).ink; }

    private:
        struct Entry
        {
            StrokeInk ink;
            Matrix transform;
        };
        std::map<StrokeId, Entry> strokes;
        std::vector<StrokeId> order;
        StrokeId next = 1;
    };

    /// The ink of a <stroke> element (asserting it can be ink).
    StrokeInk InkOfXml(std::string_view xml);
}
