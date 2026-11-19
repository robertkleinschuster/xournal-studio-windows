#pragma once

// Internal: other PDFs opened with qpdf as sources of pages. Opening flattens the annotations' appearances into the
// pages (qpdf flattenAnnotations: what a viewer shows - Hidden and NoView left out), since annotations do not come along
// as page content. ForeignPages turns pages into form XObjects of an output document (content and resources copied
// unchanged: text stays text, images keep their data and filters); the sources stay open until the output is written.

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace xournal::pdf::detail
{
    struct OpenedPdf
    {
        std::shared_ptr<QPDF> pdf;
        std::shared_ptr<std::vector<std::uint8_t>> data; // a source read from memory keeps its bytes alive
        std::vector<QPDFPageObjectHelper> pages;
        bool annotationsLeftOut = false;                  // flattening failed: the pages as they are
    };

    /// Opens a PDF file (qpdf reads it; UTF-8 path) with its annotations flattened. Throws for a file qpdf cannot read.
    OpenedPdf OpenFlattened(std::filesystem::path const& file);
    /// The same for a PDF in memory.
    OpenedPdf OpenFlattened(std::span<std::uint8_t const> data, std::string const& description);

    /// qpdf's collected warnings of a document, logged.
    void LogWarnings(QPDF& pdf, std::string const& description);

    /// A path as qpdf takes it on Windows (UTF-8).
    std::string QpdfPath(std::filesystem::path const& file);

    class ForeignPages
    {
    public:
        explicit ForeignPages(QPDF& out) : out(out) {}

        /// The form XObject (in the output) of a page of the file; nothing when the file does not exist. An index beyond
        /// the last page takes the last page.
        std::optional<QPDFObjectHandle> PageForm(std::filesystem::path const& file, int pageIndex);
        /// The first page of a PDF in memory (a copied area stored as a one-page PDF).
        QPDFObjectHandle FirstPageOf(std::span<std::uint8_t const> data);

        bool AnnotationsLeftOut() const { return annotationsLeftOut; }

        /// The form's box with its /Matrix applied (the area it draws in its own space).
        static QPDFObjectHandle::Rectangle TransformedBox(QPDFObjectHandle form);

    private:
        struct Source
        {
            OpenedPdf opened;
            std::map<int, QPDFObjectHandle> forms; // page index -> form in the output
        };
        QPDFObjectHandle FormOf(Source& source, int pageIndex);

        QPDF& out;
        std::map<std::wstring, Source> files; // by full path, as Windows compares it (upper case)
        std::vector<std::unique_ptr<Source>> memory;
        bool annotationsLeftOut = false;
    };
}
