#include "Recognition.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/pdf/PdfAreaRenderer.h"

#include <bcrypt.h>
#include <robuffer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <tuple>

namespace xournal::search
{
    using ImageKey = std::array<std::uint8_t, 32>;
    using PdfWords = std::pair<std::vector<RecognizedWord>, bool>;

    class VisualTextCache
    {
    public:
        std::mutex lock;
        std::map<ImageKey, std::vector<RecognizedWord>> images;
        std::weak_ptr<pdftext::TextDocument const> pdf;
        std::map<std::tuple<int, bool, bool>, PdfWords> pdfPages;
    };

    std::shared_ptr<VisualTextCache> CreateVisualTextCache() { return std::make_shared<VisualTextCache>(); }
}

namespace xournal::search::detail
{
    namespace
    {
        using namespace winrt::Windows::Graphics::Imaging;
        using namespace winrt::Windows::Media::Ocr;
        using namespace winrt::Windows::Storage::Streams;

        ImageKey HashImage(std::span<std::uint8_t const> data)
        {
            if (data.size() > std::numeric_limits<ULONG>::max()) ThrowInvalidData("search: image is too large");
            ImageKey key{};
            auto hr = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(data.data()),
                static_cast<ULONG>(data.size()), key.data(), static_cast<ULONG>(key.size()));
            if (hr < 0) ThrowHr("BCryptHash (OCR image)", HRESULT_FROM_NT(hr));
            return key;
        }

        void LogWinRt(std::string_view operation, winrt::hresult_error const& error)
        {
            LogError(std::format("{}: {} ({})", operation, ToUtf8(std::wstring(error.message())), DescribeHr(error.code())));
        }
    }

    struct VisualText::Impl
    {
        SearchDocument const& document;
        VisualTextCache& cache;
        OcrEngine engine{ nullptr };
        bool attempted = false;
        std::unique_ptr<pdf::PdfAreaRenderer> renderer;

        bool EnsureEngine()
        {
            if (!attempted)
            {
                attempted = true;
                try
                {
                    engine = OcrEngine::TryCreateFromUserProfileLanguages();
                    if (!engine)
                    {
                        auto languages = OcrEngine::AvailableRecognizerLanguages();
                        if (languages.Size()) engine = OcrEngine::TryCreateFromLanguage(languages.GetAt(0));
                    }
                    if (!engine) LogError("search: no Windows OCR language is installed");
                }
                catch (winrt::hresult_error const& error)
                {
                    LogWinRt("search: Windows OCR is unavailable", error);
                }
            }
            return engine != nullptr;
        }

        std::vector<RecognizedWord> Raster(std::span<std::uint8_t const> data)
        {
            InMemoryRandomAccessStream stream;
            DataWriter writer(stream);
            writer.WriteBytes({ data.data(), data.data() + data.size() });
            writer.StoreAsync().get();
            writer.DetachStream();
            stream.Seek(0);
            auto decoder = BitmapDecoder::CreateAsync(stream).get();
            auto width = decoder.PixelWidth(), height = decoder.PixelHeight();
            auto longest = std::max(width, height);
            if (!width || !height) ThrowInvalidData("search: OCR image has no pixels");
            BitmapTransform transform;
            auto limit = OcrEngine::MaxImageDimension();
            if (longest > limit)
            {
                transform.ScaledWidth(std::max(1u, static_cast<unsigned>(static_cast<uint64_t>(width) * limit / longest)));
                transform.ScaledHeight(std::max(1u, static_cast<unsigned>(static_cast<uint64_t>(height) * limit / longest)));
            }
            auto bitmap = decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied,
                transform, ExifOrientationMode::IgnoreExifOrientation, ColorManagementMode::DoNotColorManage).get();
            // Transparent pictures and rendered PDF areas are read against white paper.
            auto bytes = static_cast<uint32_t>(bitmap.PixelWidth()) * static_cast<uint32_t>(bitmap.PixelHeight()) * 4;
            Buffer pixels(bytes);
            pixels.Length(bytes);
            bitmap.CopyToBuffer(pixels);
            BYTE* bgra = nullptr;
            winrt::check_hresult(pixels.as<Windows::Storage::Streams::IBufferByteAccess>()->Buffer(&bgra));
            for (uint32_t i = 0; i < bytes; i += 4)
            {
                auto white = static_cast<BYTE>(255 - bgra[i + 3]);
                for (uint32_t channel = 0; channel < 3; ++channel) bgra[i + channel] += white;
                bgra[i + 3] = 255;
            }
            bitmap.CopyFromBuffer(pixels);
            auto result = engine.RecognizeAsync(bitmap).get();
            double sx = 1.0 / bitmap.PixelWidth(), sy = 1.0 / bitmap.PixelHeight();
            std::vector<RecognizedWord> words;
            int index = 0;
            for (auto const& line : result.Lines())
            {
                for (auto const& word : line.Words())
                {
                    auto r = word.BoundingRect();
                    words.push_back({ std::wstring(word.Text()), { r.X * sx, r.Y * sy, r.Width * sx, r.Height * sy }, index });
                }
                ++index;
            }
            return words;
        }
    };

    VisualText::VisualText(SearchDocument const& document, VisualTextCache& cache)
        : impl(new Impl{ document, cache, nullptr, false, {} })
    {}

    VisualText::~VisualText() = default;

    std::vector<RecognizedWord> VisualText::Image(std::span<std::uint8_t const> data)
    {
        auto& m = *impl;
        if (data.empty()) return {};
        auto key = HashImage(data);
        {
            std::scoped_lock guard(m.cache.lock);
            if (auto it = m.cache.images.find(key); it != m.cache.images.end()) return it->second;
        }
        if (!m.EnsureEngine()) return {};
        std::vector<RecognizedWord> words;
        try { words = m.Raster(data); }
        catch (winrt::hresult_error const& error) { LogWinRt("search: OCR of an embedded image", error); }
        catch (...) { LogException("search: OCR of an embedded image"); }
        std::scoped_lock guard(m.cache.lock);
        m.cache.images.try_emplace(key, words);
        return words;
    }

    PdfWords VisualText::PdfPage(int page, bool tryInk, bool textless, bool tryOcr)
    {
        auto& m = *impl;
        auto const& pdf = m.document.pdf;
        tryOcr = tryOcr && textless;
        if (!pdf || (!tryInk && !tryOcr)) return {};
        auto key = std::tuple(page, tryInk, tryOcr);
        {
            std::scoped_lock guard(m.cache.lock);
            if (m.cache.pdf.lock() != pdf)
            {
                m.cache.pdf = pdf;
                m.cache.pdfPages.clear();
            }
            if (auto it = m.cache.pdfPages.find(key); it != m.cache.pdfPages.end()) return it->second;
        }

        PdfWords found;
        auto size = pdf->PageSize(page);
        if (size.width <= 0 || size.height <= 0) return {};
        if (tryInk)
        {
            try
            {
                found.first = RecognizePenPaths(pdf->PenPaths(page, textless));
                found.second = !found.first.empty();
                for (auto& word : found.first)
                    word.bounds = { word.bounds.x / size.width, word.bounds.y / size.height,
                        word.bounds.width / size.width, word.bounds.height / size.height };
            }
            catch (winrt::hresult_error const& error) { LogWinRt("search: ink analysis on a PDF page", error); }
            catch (...) { LogException(std::format("search: ink analysis on PDF page {}", page + 1)); }
        }
        if (found.first.empty() && tryOcr && m.document.pdfPath && m.EnsureEngine())
        {
            try
            {
                if (!m.renderer) m.renderer = std::make_unique<xournal::pdf::PdfAreaRenderer>(*m.document.pdfPath);
                double scale = std::min(3.0, 2000.0 / std::max(size.width, size.height));
                auto raster = m.renderer->Render(page, { 0, 0, size.width, size.height }, scale);
                found.first = m.Raster(raster.png);
            }
            catch (winrt::hresult_error const& error) { LogWinRt("search: OCR on a PDF page", error); }
            catch (...) { LogException(std::format("search: OCR on PDF page {}", page + 1)); }
        }
        Log(std::format("search: PDF page {}: {} words from {}", page + 1, found.first.size(),
            found.second ? "ink analysis" : "image OCR"));
        std::scoped_lock guard(m.cache.lock);
        if (m.cache.pdf.lock() == pdf) m.cache.pdfPages.try_emplace(key, found);
        return found;
    }
}
