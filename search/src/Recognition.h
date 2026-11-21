#pragma once

#include "xournal/search/Search.h"

namespace xournal::search::detail
{
    std::vector<RecognizedWord> RecognizePenPaths(std::vector<std::vector<Point>> const& paths);

    class VisualText
    {
    public:
        VisualText(SearchDocument const& document, VisualTextCache& cache);
        ~VisualText();
        // Image words use normalized coordinates, so moving or scaling a picture does not re-run OCR.
        std::vector<RecognizedWord> Image(std::span<std::uint8_t const> data);
        // PDF words also use normalized coordinates. The flag is true when they came from ink analysis.
        std::pair<std::vector<RecognizedWord>, bool> PdfPage(int page, bool tryInk, bool textless, bool tryOcr);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
