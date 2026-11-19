#pragma once

// Attachments: files named after the document ("<document>.<filename>", domain "attach") - an attached PDF or picture
// background. Upstream copies them along when it saves under another name; io::DocumentFile does the copying.

#include "xournal/model/XoppDocument.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace xournal
{
    struct Attachment
    {
        std::optional<std::filesystem::path> source; // the file the document shows (nothing: unresolved)
        std::wstring filename;                       // the name after "<target>."
    };

    /// The document's attached PDF and picture backgrounds (clones are not attachments of their own).
    std::vector<Attachment> FindAttachments(XoppDocument const& doc);
}
