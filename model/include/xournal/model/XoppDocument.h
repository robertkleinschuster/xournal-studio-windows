#pragma once

// A Xournal++ document: the <xournal> element as loaded (title, preview and whatever else other programs wrote) and its
// pages, taken out of the tree and put back on save (see Snapshot.h). Reading and writing files is the io module's job;
// this is the document in memory with its references (background PDF, picture backgrounds) resolved.

#include "xournal/model/DocumentNames.h"
#include "xournal/model/Geometry.h"
#include "xournal/model/XoppPage.h"
#include "xournal/model/Xml.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xournal
{
    /// The first PDF background's reference as found in the file (both may be missing).
    struct PdfReference
    {
        std::optional<std::wstring> domain;
        std::optional<std::wstring> filename;
    };

    /// An attachment copied next to a file saved under another name (source -> copy, full paths).
    struct AttachmentCopy
    {
        std::filesystem::path source;
        std::filesystem::path copy;
    };
    using AttachmentCopies = std::vector<AttachmentCopy>;

    class XoppDocument
    {
    public:
        using path = std::filesystem::path;
        using PageList = std::vector<std::shared_ptr<XoppPage>>;

        /// A new document with one graph page (A4).
        static std::unique_ptr<XoppDocument> CreateNew();

        /// A document with one page per PDF page, the PDF as background.
        static std::unique_ptr<XoppDocument> FromPdf(path pdf, std::vector<Size> const& pageSizes);

        /// A loaded <xournal> element: pages are taken out, references resolved as from `referenceBase` (the file's
        /// path, or for a recovery copy the document's). A MrWriter document is read as upstream reads it and becomes a
        /// <xournal> one (as upstream saves it). Throws (invalid data) when it is neither.
        static std::unique_ptr<XoppDocument> FromXml(XmlElementPtr root, path const& referenceBase);

        explicit XoppDocument(XmlElementPtr root);
        XoppDocument(XoppDocument const&) = delete;
        XoppDocument& operator=(XoppDocument const&) = delete;

        /// The <xournal> element without its pages.
        XmlElement& Root() const { return *root; }

        PageList& Pages() { return pages; }
        PageList const& Pages() const { return pages; }

        std::optional<path> const& FilePath() const { return filePath; }
        void SetFilePath(std::optional<path> file) { filePath = std::move(file); }

        /// The resolved path of the background PDF, if any (it may be missing).
        std::optional<path> const& PdfPath() const { return pdfPath; }
        /// The background PDF was renamed or moved: the document refers to it there from now on (written on the next
        /// save, as the old reference no longer names it).
        void MovePdf(path newPath) { pdfPath = std::move(newPath); }

        PdfReference const& LoadedPdfReference() const { return loadedPdfReference; }
        void SetLoadedPdfReference(PdfReference reference) { loadedPdfReference = std::move(reference); }

        /// When the document was created in this app (for the default name); nothing for loaded files.
        std::optional<DateTime> const& CreatedAt() const { return createdAt; }

        /// The files the document consists of and shows: its file, its background PDF, its picture backgrounds.
        std::vector<path> Files() const;

        /// Whether `item` (a file or a folder) is or holds one of the Files: what happens to it there (deleted, renamed,
        /// discarded, pulled) happens to the open document.
        bool Uses(path const& item) const;

        /// The name the first save suggests (see DocumentNames::NameFor).
        std::wstring SuggestedName() const;

        /// Where PDF references of a copy are made relative to: the document's file, or for a document never saved a
        /// file next to its PDF (any place does when it has none).
        path ReferenceBase() const;

        /// The pages of the background PDF that these pages show (0-based, ascending, each once).
        std::vector<int> PdfPagesOf(std::vector<int> const& indices) const;

        /// Save As wrote these copies of the attachments: the document refers to its own copies from now on.
        void AdoptAttachmentCopies(AttachmentCopies const& copies);

    private:
        XmlElementPtr root;
        PageList pages;
        std::optional<path> filePath;
        std::optional<path> pdfPath;
        PdfReference loadedPdfReference; // written back unchanged while it still names the PDF
        std::optional<DateTime> createdAt;
    };
}
