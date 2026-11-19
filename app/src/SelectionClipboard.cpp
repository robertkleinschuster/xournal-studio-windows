#include "SelectionClipboard.h"

#include "Bitmaps.h"
#include "InkConvert.h"
#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/io/XmlText.h"
#include "xournal/model/Strokes.h"
#include "xournal/model/XoppClip.h"
#include "xournal/model/XoppImage.h"
#include "xournal/pdf/ClipPicture.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <span>
#include <vector>

namespace xournal::app
{
    namespace
    {
        constexpr wchar_t ClipFormatName[] = L"Xournal Studio Clip";
        constexpr double PicturePixelsPerPoint = 2;  // the copy's picture for other apps: 192 DPI
        constexpr double PointsPerPixel = 72.0 / 96; // a pasted picture's pixel = a DIP

        UINT Register(wchar_t const* name)
        {
            UINT format = RegisterClipboardFormatW(name);
            if (!format) ThrowLastError(std::format("RegisterClipboardFormat({})", ToUtf8(name)));
            return format;
        }

        // The clipboard, open while this lives.
        class OpenClipboardScope
        {
        public:
            explicit OpenClipboardScope(HWND owner)
            {
                if (!OpenClipboard(owner)) ThrowLastError("OpenClipboard");
            }
            ~OpenClipboardScope() { CloseClipboard(); }
            OpenClipboardScope(OpenClipboardScope const&) = delete;
            OpenClipboardScope& operator=(OpenClipboardScope const&) = delete;
        };

        void SetBytes(UINT format, std::span<std::uint8_t const> bytes)
        {
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
            if (!memory) ThrowLastError("GlobalAlloc (clipboard)");
            void* p = GlobalLock(memory);
            if (!p)
            {
                GlobalFree(memory);
                ThrowLastError("GlobalLock (clipboard)");
            }
            std::memcpy(p, bytes.data(), bytes.size());
            GlobalUnlock(memory);
            if (!SetClipboardData(format, memory))
            {
                GlobalFree(memory); // still ours: the clipboard did not take it
                ThrowLastError(std::format("SetClipboardData({})", format));
            }
        }

        std::vector<std::uint8_t> GetBytes(UINT format)
        {
            HANDLE data = GetClipboardData(format);
            if (!data) ThrowLastError(std::format("GetClipboardData({})", format));
            SIZE_T size = GlobalSize(data);
            void const* p = GlobalLock(data);
            if (!p) ThrowLastError("GlobalLock (clipboard data)");
            std::vector<std::uint8_t> bytes(static_cast<std::uint8_t const*>(p), static_cast<std::uint8_t const*>(p) + size);
            GlobalUnlock(data);
            return bytes;
        }

        // A device-independent bitmap of the picture on white (other apps show its unused fourth byte as black).
        std::vector<std::uint8_t> DibOnWhite(CpuBitmap const& picture)
        {
            BITMAPINFOHEADER header{ sizeof(header) };
            header.biWidth = static_cast<LONG>(picture.width);
            header.biHeight = static_cast<LONG>(picture.height); // bottom-up
            header.biPlanes = 1;
            header.biBitCount = 32;
            header.biCompression = BI_RGB;
            size_t stride = static_cast<size_t>(picture.width) * 4;
            std::vector<std::uint8_t> dib(sizeof(header) + stride * picture.height);
            std::memcpy(dib.data(), &header, sizeof(header));
            for (UINT y = 0; y < picture.height; ++y)
            {
                std::uint8_t const* from = picture.pixels.data() + y * stride;
                std::uint8_t* to = dib.data() + sizeof(header) + (picture.height - 1 - y) * stride;
                for (UINT x = 0; x < picture.width; ++x)
                {
                    std::uint8_t a = from[x * 4 + 3];
                    for (int c = 0; c < 3; ++c) to[x * 4 + c] = static_cast<std::uint8_t>(from[x * 4 + c] + (255 - a)); // premultiplied over white
                    to[x * 4 + 3] = 255;
                }
            }
            return dib;
        }

        // A packed DIB (CF_DIB) as a .bmp file for WIC's BMP decoder: the file header says where the pixels start.
        std::vector<std::uint8_t> BmpFileOf(std::vector<std::uint8_t> const& dib)
        {
            if (dib.size() < sizeof(BITMAPINFOHEADER)) ThrowInvalidData(std::format("clipboard bitmap of {} bytes", dib.size()));
            BITMAPINFOHEADER header;
            std::memcpy(&header, dib.data(), sizeof(header));
            DWORD masks = (header.biSize == sizeof(BITMAPINFOHEADER) && header.biCompression == BI_BITFIELDS) ? 12 : 0;
            DWORD colors = header.biClrUsed ? header.biClrUsed : (header.biBitCount <= 8 ? (1u << header.biBitCount) : 0);
            BITMAPFILEHEADER file{};
            file.bfType = 0x4D42; // "BM"
            file.bfSize = static_cast<DWORD>(sizeof(file) + dib.size());
            file.bfOffBits = static_cast<DWORD>(sizeof(file) + header.biSize + masks + colors * sizeof(RGBQUAD));
            std::vector<std::uint8_t> bmp(sizeof(file));
            std::memcpy(bmp.data(), &file, sizeof(file));
            bmp.insert(bmp.end(), dib.begin(), dib.end());
            return bmp;
        }
    }

    SelectionClipboard::SelectionClipboard(HWND owner, inkpages::WritingSurface& surface, DocumentView& view)
        : owner(owner), surface(surface), view(view), clipFormat(Register(ClipFormatName)), pngFormat(Register(L"PNG"))
    {
    }

    void SelectionClipboard::Copy(inkpages::Clip const& clip)
    {
        float width = clip.bounds.Width(), height = clip.bounds.Height();
        std::vector<XmlElementPtr> elements;
        for (auto const& stroke : clip.strokes) elements.push_back(Strokes::ToXml(InkConvert::FromInkStroke(stroke)));
        if (FilePictures* pictures = view.Pictures())
        {
            for (auto& e : pictures->CopiesOf(clip.pictures, { clip.bounds.left, clip.bounds.top })) elements.push_back(e);
        }
        auto xml = XoppClip::Create({ 0, 0, width, height }, elements);
        auto bytes = io::SerializeXml(*xml);
        {
            OpenClipboardScope open(owner);
            if (!EmptyClipboard()) ThrowLastError("EmptyClipboard");
            SetBytes(clipFormat, bytes);
        }
        Log(std::format("clipboard: {} strokes, {} pictures/texts copied ({} bytes)", clip.strokes.size(),
            elements.size() - clip.strokes.size(), bytes.size()));
        AddPicturesLater(xml, GetClipboardSequenceNumber());
    }

    void SelectionClipboard::AddPicturesLater(XmlElementPtr clip, DWORD sequence)
    {
        UiQueue::RunInBackground("clipboard picture", [this, alive = std::weak_ptr<bool>(alive), clip, sequence]
        {
            HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(com)) ThrowHr("CoInitializeEx (clipboard picture)", com);
            struct Uninit { ~Uninit() { CoUninitialize(); } } uninit;
            auto picture = pdf::RenderClip(*clip, PicturePixelsPerPoint);
            auto dib = DibOnWhite(*Bitmaps::Decode(picture.png));
            UiQueue::Post("clipboard picture", [this, alive, sequence, png = std::move(picture.png), dib = std::move(dib)]
            {
                if (alive.expired()) return;
                if (GetClipboardSequenceNumber() != sequence)
                {
                    Log("clipboard: changed meanwhile - the copy's picture is not added");
                    return;
                }
                OpenClipboardScope open(owner); // still the owner: added to the copy without emptying it
                SetBytes(pngFormat, png);
                SetBytes(CF_DIB, dib);
                Log(std::format("clipboard: picture added (PNG {} bytes)", png.size()));
            });
        });
    }

    std::optional<inkpages::Clip> SelectionClipboard::Paste(inkpages::PointF at)
    {
        if (!view.Pictures()) return std::nullopt;
        if (IsClipboardFormatAvailable(clipFormat)) return PasteClip(at);
        if (IsClipboardFormatAvailable(pngFormat) || IsClipboardFormatAvailable(CF_DIB)) return PastePicture(at);
        Log("clipboard: nothing of the app's or a picture - Windows ink's clipboard");
        return std::nullopt;
    }

    std::optional<inkpages::Clip> SelectionClipboard::PasteClip(inkpages::PointF at)
    {
        std::vector<std::uint8_t> bytes;
        {
            OpenClipboardScope open(owner);
            bytes = GetBytes(clipFormat);
        }
        // The global memory may be larger than what was put there: the XML ends at its last '>'.
        auto end = std::find(bytes.rbegin(), bytes.rend(), '>');
        bytes.resize(static_cast<size_t>(bytes.rend() - end));
        auto clip = io::ParseXml(bytes);
        if (clip->Name() != L"clip") ThrowInvalidData(std::format("clipboard: a <{}> instead of a clip", ToUtf8(clip->Name())));

        inkpages::Clip result;
        std::vector<XmlElementPtr> objects;
        for (auto const& e : XoppClip::Place(*clip, Matrix{}))
        {
            if (e->Name() == L"stroke")
            {
                if (auto ink = Strokes::InkOf(*e)) result.strokes.push_back(InkConvert::ToInkStroke(*ink)); // relative to `at`
                else LogError("clipboard: a pasted stroke Windows ink cannot draw - left out");
            }
            else if (XoppImage::IsImage(*e) || e->Name() == L"text") objects.push_back(XoppClip::Transformed(*e, Matrix::Translation(at.x, at.y)));
            else LogError(std::format("clipboard: a pasted <{}> - left out", ToUtf8(e->Name())));
        }
        result.pictures = view.Pictures()->Add(objects);
        Size size = XoppClip::GetSize(*clip);
        result.bounds = { at.x, at.y, at.x + static_cast<float>(size.width), at.y + static_cast<float>(size.height) };
        Log(std::format("clipboard: pasted {} strokes, {} pictures/texts", result.strokes.size(), result.pictures.size()));
        return result;
    }

    std::optional<inkpages::Clip> SelectionClipboard::PastePicture(inkpages::PointF at)
    {
        std::vector<std::uint8_t> png;
        std::shared_ptr<CpuBitmap> pixels;
        {
            OpenClipboardScope open(owner);
            if (IsClipboardFormatAvailable(pngFormat))
            {
                png = GetBytes(pngFormat);
                pixels = Bitmaps::Decode(png); // also checks it is a picture
            }
            else
            {
                pixels = Bitmaps::Decode(BmpFileOf(GetBytes(CF_DIB)));
                png = Bitmaps::EncodePng(*pixels);
            }
        }
        double width = pixels->width * PointsPerPixel, height = pixels->height * PointsPerPixel;
        // A picture wider than the page under it is made as wide as the page (proportional).
        if (auto page = surface.Layout().NearestPage(at))
        {
            double pageWidth = surface.Layout().Page(*page).Width();
            if (width > pageWidth)
            {
                height *= pageWidth / width;
                width = pageWidth;
            }
        }
        auto element = XoppImage::Create({ at.x, at.y, width, height }, png, static_cast<int>(pixels->width),
            static_cast<int>(pixels->height));
        inkpages::Clip result;
        result.pictures = view.Pictures()->Add({ element });
        result.bounds = { at.x, at.y, at.x + static_cast<float>(width), at.y + static_cast<float>(height) };
        Log(std::format("clipboard: pasted a picture of another app ({} x {} pixels)", pixels->width, pixels->height));
        return result;
    }
}
