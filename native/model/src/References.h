#pragma once

// The document's references to other files, as upstream reads and writes them:
//   PDF background: domain "absolute" + filename = a path (relative to the .xopp's folder unless rooted, '/'
//   separators; upstream Util::normalizeAssetPath), or domain "attach" + filename "bg.pdf" = "<name>.xopp.bg.pdf".
//   Picture background (LoadHandler::setBgPixmap): "attach" = "<document>.<filename>", "absolute" = the file (relative
//   to the document's folder if not rooted), "clone" = the background of page number <filename>.
// A loaded reference is written back as it was while it still names its file from where the document is saved;
// otherwise it is written as a path (relative, unless the loaded one was rooted).

#include "xournal/model/XoppDocument.h"

#include <filesystem>
#include <optional>
#include <string>

namespace xournal::References
{
    using std::filesystem::path;

    /// The file a picture background names from a document at `target`.
    path PictureFile(path const& target, std::wstring const& domain, std::wstring const& filename);

    std::optional<path> ResolvePdf(path const& referenceBase, PdfReference const& reference);
    void ResolvePictureBackgrounds(XoppDocument::PageList const& pages, path const& referenceBase);

    /// Rewrites the first PDF background of a snapshot (`firstPdf`) or any later one (only the first names the file;
    /// Xournal reads the attributes in this order: type, domain, filename, pageno).
    void WritePdfReference(XoppDocument const& doc, XmlElement& background, bool firstPdf, path const& target,
        AttachmentCopies const& copies);

    /// Rewrites a picture background's reference for a document saved at `target` (`pageIndex`: its page's position in
    /// the snapshot, for a clone that must name an earlier page).
    void WritePictureReference(XoppDocument const& doc, XoppPage const& page, XmlElement& background, path const& target,
        int pageIndex, AttachmentCopies const& copies);

    /// The copy made for this write of `source`, if any.
    std::optional<path> CopyOf(AttachmentCopies const& copies, path const& source);
}
