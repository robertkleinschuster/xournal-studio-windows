#pragma once

// A page's <background> element as upstream writes it: type "solid" (color, style, config), "pdf" (domain, filename,
// pageno) or "pixmap" (domain absolute/attach/clone, filename).

#include "xournal/model/Color.h"
#include "xournal/model/Xml.h"

#include <string>

namespace xournal
{
    /// The "style" of a solid background (upstream PageTypeFormat; "lined" = ruled with a margin line).
    enum class BackgroundStyle { Plain, Lined, Ruled, Graph, Dotted, Staves, IsoDotted, IsoGraph };

    namespace Background
    {
        bool IsPdf(XmlElement const& background);
        bool IsPixmap(XmlElement const& background);

        /// 0-based page of the background PDF; older files have values like "3ll".
        int PdfPageIndex(XmlElement const& background);

        Color ColorOf(XmlElement const& background);
        BackgroundStyle StyleOf(XmlElement const& background);

        /// <background type="solid" color="#ffffffff" style="..."/>
        XmlElementPtr Solid(std::wstring style);

        /// <background type="pdf" pageno="n"/> (only a document's first PDF background names the file).
        XmlElementPtr Pdf(int pdfPageIndex);
    }
}
