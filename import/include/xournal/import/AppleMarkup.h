#pragma once

// What Apple's markup (PDFKit/AnnotationKit on iPad and Mac) keeps beside a PDF annotation it wrote: the annotation's
// /AAPL:AKExtras dictionary holds a base64 binary plist (an NSKeyedArchiver archive) under /AAPL:AKAnnotationV2 or
// /AAPL:AKAnnotationObject. Its root object names the annotation's class (AKInkAnnotation2: a PencilKit drawing in
// "drawing", in a coordinate space of "drawingSize"; AKImageAnnotation, AKTextBoxAnnotation, ...). Not documented by
// Apple. Port of the C# AppleMarkup (which used plist-cil).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xournal::import
{
    struct AppleMarkup
    {
        static constexpr std::wstring_view InkClass = L"AKInkAnnotation2";

        std::wstring className;
        std::optional<std::vector<std::uint8_t>> drawing;
        double drawingWidth = 0, drawingHeight = 0;

        /// The archive's root object; throws xournal::Error (invalid data) when it is not such an archive.
        static AppleMarkup Read(std::string_view base64);

        /// The bytes of a base64 text (as the PDF strings of /AAPL:AKExtras hold them); throws xournal::Error (invalid data).
        static std::vector<std::uint8_t> DecodeBase64(std::string_view text);

        /// The archive of an ink annotation as iPadOS writes it (the object graph of the user's PDFs), base64: `drawing` = a
        /// PencilKit drawing in a coordinate space of `drawingWidth` x `drawingHeight`, `rectangle` = the annotation's
        /// place on the page (x, y = the lower left corner in PDF user space, width, height), `uuid` its identifier.
        static std::string WriteInk(std::span<std::uint8_t const> drawing, double drawingWidth, double drawingHeight,
                                    double x, double y, double width, double height, std::string_view uuid);
    };
}
