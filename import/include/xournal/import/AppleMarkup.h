#pragma once

// What Apple's markup (PDFKit/AnnotationKit on iPad and Mac) keeps beside a PDF annotation it wrote: the annotation's
// /AAPL:AKExtras dictionary holds a base64 binary plist (an NSKeyedArchiver archive) under /AAPL:AKAnnotationV2 or
// /AAPL:AKAnnotationObject. Its root object names the annotation's class (AKInkAnnotation2: a PencilKit drawing in
// "drawing", in a coordinate space of "drawingSize"; AKImageAnnotation, AKTextBoxAnnotation, ...). Not documented by
// Apple. Port of the C# AppleMarkup (which used plist-cil).

#include <cstdint>
#include <optional>
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
    };
}
