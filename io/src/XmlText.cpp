#include "xournal/io/XmlText.h"

#include "xournal/base/Error.h"

#include <windows.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <xmllite.h>

#include <format>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace xournal::io
{
    namespace
    {
        std::wstring Qualified(IXmlReader& reader)
        {
            wchar_t const* name = nullptr;
            UINT length = 0;
            Check("IXmlReader::GetQualifiedName", reader.GetQualifiedName(&name, &length));
            return { name, length };
        }

        std::wstring NamespaceUri(IXmlReader& reader)
        {
            wchar_t const* uri = nullptr;
            UINT length = 0;
            Check("IXmlReader::GetNamespaceUri", reader.GetNamespaceUri(&uri, &length));
            return { uri, length };
        }

        std::wstring Value(IXmlReader& reader)
        {
            wchar_t const* value = nullptr;
            UINT length = 0;
            Check("IXmlReader::GetValue", reader.GetValue(&value, &length));
            return { value, length };
        }

        std::wstring LocalName(IXmlReader& reader)
        {
            wchar_t const* name = nullptr;
            UINT length = 0;
            Check("IXmlReader::GetLocalName", reader.GetLocalName(&name, &length));
            return { name, length };
        }

        void ReadAttributes(IXmlReader& reader, XmlElement& element)
        {
            HRESULT hr = Check("IXmlReader::MoveToFirstAttribute", reader.MoveToFirstAttribute());
            while (hr == S_OK)
            {
                if (!reader.IsDefault())
                {
                    element.AddAttribute({ Qualified(reader), Value(reader), NamespaceUri(reader) });
                }
                hr = Check("IXmlReader::MoveToNextAttribute", reader.MoveToNextAttribute());
            }
            Check("IXmlReader::MoveToElement", reader.MoveToElement());
        }

        [[noreturn]] void ThrowParseError(IXmlReader& reader, HRESULT hr)
        {
            UINT line = 0, column = 0;
            // Positions only describe the error; failing to get them leaves 0 in the message.
            reader.GetLineNumber(&line);
            reader.GetLinePosition(&column);
            ThrowHr(std::format("XML at line {}, column {}", line, column), hr);
        }

        // "prefix:local" as XmlLite's writer takes it.
        std::pair<std::wstring, std::wstring> SplitName(std::wstring const& qualified)
        {
            size_t colon = qualified.find(L':');
            if (colon == std::wstring::npos)
            {
                return { {}, qualified };
            }
            return { qualified.substr(0, colon), qualified.substr(colon + 1) };
        }

        wchar_t const* OrNull(std::wstring const& s) { return s.empty() ? nullptr : s.c_str(); }

        // XmlLite's WriteString and WriteWhitespace write a line feed as CR LF (measured); upstream's files, and the tree
        // (the parser turns CR LF into LF), have LF. The pieces between line feeds go through the writer's own call
        // (escaped), each line feed as raw "\n".
        void WriteLines(IXmlWriter& writer, std::wstring const& text, bool whitespace)
        {
            size_t start = 0;
            while (true)
            {
                size_t end = text.find(L'\n', start);
                std::wstring piece = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
                if (!piece.empty())
                {
                    if (whitespace) Check("IXmlWriter::WriteWhitespace", writer.WriteWhitespace(piece.c_str()));
                    else Check("IXmlWriter::WriteString", writer.WriteString(piece.c_str()));
                }
                if (end == std::wstring::npos) break;
                Check("IXmlWriter::WriteRaw", writer.WriteRaw(L"\n"));
                start = end + 1;
            }
        }

        void WriteElement(IXmlWriter& writer, XmlElement const& element)
        {
            auto [prefix, local] = SplitName(element.Name());
            Check("IXmlWriter::WriteStartElement",
                writer.WriteStartElement(OrNull(prefix), local.c_str(), OrNull(element.NamespaceUri())));
            for (auto const& attribute : element.Attributes())
            {
                auto [attributePrefix, attributeLocal] = SplitName(attribute.name);
                // Namespace declarations ("xmlns", "xmlns:p") are written as such: XmlLite wants no URI for them.
                bool declaration = attribute.name == L"xmlns" || attributePrefix == L"xmlns";
                Check("IXmlWriter::WriteAttributeString", writer.WriteAttributeString(OrNull(attributePrefix),
                    attributeLocal.c_str(), declaration ? nullptr : OrNull(attribute.namespaceUri), attribute.value.c_str()));
            }
            for (auto const& node : element.Nodes())
            {
                switch (node.kind)
                {
                case XmlNodeKind::Element: WriteElement(writer, *node.element); break;
                case XmlNodeKind::Text: WriteLines(writer, node.text, false); break;
                case XmlNodeKind::Whitespace: WriteLines(writer, node.text, true); break;
                case XmlNodeKind::CData: Check("IXmlWriter::WriteCData", writer.WriteCData(node.text.c_str())); break;
                case XmlNodeKind::Comment: Check("IXmlWriter::WriteComment", writer.WriteComment(node.text.c_str())); break;
                case XmlNodeKind::ProcessingInstruction:
                    Check("IXmlWriter::WriteProcessingInstruction",
                        writer.WriteProcessingInstruction(node.target.c_str(), node.text.c_str()));
                    break;
                }
            }
            // Full end tags for elements with content, "<x/>" for empty ones (as upstream writes them).
            if (element.Nodes().empty())
            {
                Check("IXmlWriter::WriteEndElement", writer.WriteEndElement());
            }
            else
            {
                Check("IXmlWriter::WriteFullEndElement", writer.WriteFullEndElement());
            }
        }
    }

    XmlElementPtr ParseXml(std::span<std::uint8_t const> text)
    {
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(text.data(), static_cast<UINT>(text.size())));
        if (!stream)
        {
            ThrowHr("SHCreateMemStream", E_OUTOFMEMORY);
        }
        ComPtr<IXmlReader> reader;
        Check("CreateXmlReader", CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr));
        Check("IXmlReader::SetProperty(DtdProcessing)", reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit));
        Check("IXmlReader::SetInput", reader->SetInput(stream.Get()));

        XmlElementPtr root;
        std::vector<XmlElement*> open; // the elements being read, innermost last
        XmlNodeType type = XmlNodeType_None;
        for (;;)
        {
            HRESULT hr = reader->Read(&type);
            if (hr == S_FALSE)
            {
                break;
            }
            if (FAILED(hr))
            {
                ThrowParseError(*reader.Get(), hr);
            }
            switch (type)
            {
            case XmlNodeType_Element:
            {
                bool empty = reader->IsEmptyElement() != FALSE;
                auto element = std::make_shared<XmlElement>(Qualified(*reader.Get()), NamespaceUri(*reader.Get()));
                ReadAttributes(*reader.Get(), *element);
                XmlElement* raw = element.get();
                if (open.empty())
                {
                    root = std::move(element);
                }
                else
                {
                    open.back()->Append(std::move(element));
                }
                if (!empty)
                {
                    open.push_back(raw);
                }
                break;
            }
            case XmlNodeType_EndElement:
                open.pop_back();
                break;
            case XmlNodeType_Text:
            case XmlNodeType_Whitespace:
            case XmlNodeType_CDATA:
            case XmlNodeType_Comment:
            case XmlNodeType_ProcessingInstruction:
            {
                if (open.empty())
                {
                    break; // outside the document element (prolog, epilog): not part of the document
                }
                XmlNodeKind kind = type == XmlNodeType_Text ? XmlNodeKind::Text
                    : type == XmlNodeType_Whitespace ? XmlNodeKind::Whitespace
                    : type == XmlNodeType_CDATA ? XmlNodeKind::CData
                    : type == XmlNodeType_Comment ? XmlNodeKind::Comment
                    : XmlNodeKind::ProcessingInstruction;
                XmlNode node{ kind, nullptr, Value(*reader.Get()), {} };
                if (kind == XmlNodeKind::ProcessingInstruction)
                {
                    node.target = LocalName(*reader.Get());
                }
                open.back()->AppendNode(std::move(node));
                break;
            }
            default:
                break; // the XML declaration, a document type (refused by the reader before it gets here)
            }
        }
        if (!root)
        {
            ThrowInvalidData("XML without a document element");
        }
        return root;
    }

    std::vector<std::uint8_t> SerializeXml(XmlElement const& root)
    {
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(nullptr, 0));
        if (!stream)
        {
            ThrowHr("SHCreateMemStream", E_OUTOFMEMORY);
        }
        ComPtr<IXmlWriter> writer;
        Check("CreateXmlWriter", CreateXmlWriter(__uuidof(IXmlWriter), reinterpret_cast<void**>(writer.GetAddressOf()), nullptr));
        ComPtr<IUnknown> output;
        Check("CreateXmlWriterOutputWithEncodingName",
            CreateXmlWriterOutputWithEncodingName(stream.Get(), nullptr, L"utf-8", output.GetAddressOf()));
        Check("IXmlWriter::SetProperty(ByteOrderMark)", writer->SetProperty(XmlWriterProperty_ByteOrderMark, FALSE));
        Check("IXmlWriter::SetOutput", writer->SetOutput(output.Get()));
        Check("IXmlWriter::WriteStartDocument", writer->WriteStartDocument(XmlStandalone_No));
        WriteElement(*writer.Get(), root);
        Check("IXmlWriter::WriteEndDocument", writer->WriteEndDocument());
        Check("IXmlWriter::Flush", writer->Flush());

        ULARGE_INTEGER size{};
        Check("IStream_Size", IStream_Size(stream.Get(), &size));
        Check("IStream_Reset", IStream_Reset(stream.Get()));
        std::vector<std::uint8_t> bytes(static_cast<size_t>(size.QuadPart));
        Check("IStream_Read", IStream_Read(stream.Get(), bytes.data(), static_cast<ULONG>(bytes.size())));
        return bytes;
    }
}
