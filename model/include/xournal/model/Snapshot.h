#pragma once

// The complete document as it would be saved, as a detached tree: taken on the ink's thread (the pages' ink is read),
// written on any thread (io::DocumentFile::Write). References are written as from the path the file is (or would be)
// saved at.

#include "xournal/model/XoppDocument.h"

namespace xournal
{
    /// The <xournal> element with all pages; sets the creator (and a missing file version) on the document's root, as
    /// every save does. `copies`: attachments copied next to `referenceBase` for this write (Save As).
    XmlElementPtr TakeSnapshot(XoppDocument& doc, std::filesystem::path const& referenceBase, AttachmentCopies const& copies = {});
}
