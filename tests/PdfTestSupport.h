#pragma once

// Helpers of the pdf module's tests: pictures encoded with WIC, a source PDF built with qpdf, and inspection of written
// PDFs (text through content streams, nested forms and the fonts' ToUnicode maps; images; operators).

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace xournal::tests
{
    /// Encoded pictures (WIC): a gradient; alpha < 255 on the right half when withAlpha.
    std::vector<std::uint8_t> MakePng(unsigned width, unsigned height, bool withAlpha);
    std::vector<std::uint8_t> MakeJpeg(unsigned width, unsigned height);

    std::vector<std::uint8_t> ReadBytes(std::filesystem::path const& file);

    struct SourcePdf
    {
        std::vector<std::uint8_t> jpeg; // the image on page 1, as stored (DCTDecode)
    };

    /// Writes a two-page source PDF: page 1 (595 x 842): Helvetica text "BACKGROUNDTEXT page one", a JPEG image, a Stamp
    /// annotation showing "STAMPTOKEN", a hidden one showing "HIDDENTOKEN", a link; page 2 (420 x 298): "SECONDPAGE".
    /// /Info has the author "Secret Person". `pageTexts` adds pages with these texts (for the appender).
    SourcePdf WriteSourcePdf(std::filesystem::path const& file, std::vector<std::string> const& pageTexts = {});

    struct ImageInfo
    {
        std::string filter;   // "/DCTDecode", "/FlateDecode", ...
        std::string rawData;  // the stream data as stored
        bool softMask = false;
        QPDFObjGen id;
    };

    /// What a page draws, through its content and nested form XObjects.
    struct PageContents
    {
        std::string text;                                               // shown text, decoded
        std::map<std::string, int> operators;                           // operator -> count
        std::vector<std::pair<std::string, std::string>> operatorLines; // (operator, operands) in order
        std::vector<ImageInfo> images;
        int forms = 0;

        std::vector<std::string> OperandsOf(std::string const& op) const;
    };
    PageContents Inspect(QPDFPageObjectHelper page);

    /// A written PDF opened with qpdf (warnings while reading it fail the test).
    struct OpenPdf
    {
        std::shared_ptr<QPDF> pdf;
        std::vector<QPDFPageObjectHelper> pages;
    };
    OpenPdf Open(std::filesystem::path const& file);

    /// Copies a written PDF to %XOURNAL_PDF_TEST_OUTPUT% (if set) for a look at it (e.g. rendering it).
    void KeepForInspection(std::filesystem::path const& file, std::wstring const& name);

    /// The 'maxp' numGlyphs of a TrueType font file; -1 without one.
    int NumGlyphs(std::string const& font);

    /// The table tags of a TrueType font file, in directory order.
    std::vector<std::string> TableTags(std::string const& font);
}
