#pragma once

// The XML tree's text form, read and written by XmlLite (Windows' XML parser). Everything the tree keeps survives a
// round trip; DTDs are refused (Xournal++ never writes one, and they could expand entities without bound).

#include "xournal/model/Xml.h"

#include <cstdint>
#include <span>
#include <vector>

namespace xournal::io
{
    /// The document element of an XML text (the encoding as the text declares it). Throws xournal::Error with XmlLite's
    /// HRESULT and the line and column of a malformed text.
    XmlElementPtr ParseXml(std::span<std::uint8_t const> text);

    /// The element as UTF-8 XML (no byte-order mark) with the declaration upstream writes (standalone="no").
    std::vector<std::uint8_t> SerializeXml(XmlElement const& root);
}
