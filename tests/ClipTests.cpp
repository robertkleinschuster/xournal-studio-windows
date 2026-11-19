// The app's clipboard: a selection as a <clip> of upstream elements (written as XML, read back and placed), and the
// clip as a picture for other apps.

#include "TestSupport.h"

#include "xournal/io/XmlText.h"
#include "xournal/model/XoppClip.h"
#include "xournal/model/XoppText.h"
#include "xournal/pdf/ClipPicture.h"

#include <shlwapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <gtest/gtest.h>

using Microsoft::WRL::ComPtr;

namespace xournal::tests
{
    namespace
    {
        struct Decoded
        {
            UINT width = 0, height = 0;
            std::vector<BYTE> bgra; // straight alpha
            BYTE AlphaAt(UINT x, UINT y) const { return bgra[(static_cast<size_t>(y) * width + x) * 4 + 3]; }
        };

        Decoded DecodePng(std::vector<std::uint8_t> const& png)
        {
            Decoded out;
            ComPtr<IWICImagingFactory> wic;
            EXPECT_HRESULT_SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
            ComPtr<IStream> stream;
            stream.Attach(SHCreateMemStream(png.data(), static_cast<UINT>(png.size())));
            ComPtr<IWICBitmapDecoder> decoder;
            EXPECT_HRESULT_SUCCEEDED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder));
            ComPtr<IWICBitmapFrameDecode> frame;
            EXPECT_HRESULT_SUCCEEDED(decoder->GetFrame(0, &frame));
            ComPtr<IWICFormatConverter> converter;
            EXPECT_HRESULT_SUCCEEDED(wic->CreateFormatConverter(&converter));
            EXPECT_HRESULT_SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                nullptr, 0, WICBitmapPaletteTypeCustom));
            EXPECT_HRESULT_SUCCEEDED(converter->GetSize(&out.width, &out.height));
            out.bgra.resize(static_cast<size_t>(out.width) * out.height * 4);
            EXPECT_HRESULT_SUCCEEDED(converter->CopyPixels(nullptr, out.width * 4, static_cast<UINT>(out.bgra.size()), out.bgra.data()));
            return out;
        }

        XmlElementPtr SampleClip()
        {
            // A horizontal stroke at y 10 and a text below it, in page units of a page whose selection starts at 100,200.
            auto stroke = Xml(R"(<stroke tool="pen" color="#ff0000ff" width="2">100 210 140 210</stroke>)");
            auto text = Xml(R"(<text font="Sans" size="12" x="100" y="220" color="#000000ff">Zeile 1
Zeile 2</text>)");
            return XoppClip::Create({ 100, 200, 40, 40 }, { stroke, text });
        }
    }

    TEST(Clip, AClipIsWrittenAndReadBackAndPlacedWithItsTexts)
    {
        auto bytes = io::SerializeXml(*SampleClip());
        auto clip = io::ParseXml(bytes);
        EXPECT_EQ(L"clip", clip->Name());
        EXPECT_EQ((Size{ 40, 40 }), XoppClip::GetSize(*clip));

        auto placed = XoppClip::Place(*clip, Matrix::Translation(10, 20));
        ASSERT_EQ(2u, placed.size());
        EXPECT_EQ(L"10 30 50 30", placed[0]->Value());
        EXPECT_EQ(L"#ff0000ff", placed[0]->AttributeOr(L"color", L""));
        auto text = XoppText::Read(*placed[1]);
        EXPECT_EQ(L"Zeile 1\nZeile 2", text.content) << "the text's line end stays LF";
        EXPECT_EQ(10, text.transform.m31);
        EXPECT_EQ(40, text.transform.m32);
    }

    TEST(Clip, AClipIsDrawnAsATransparentPicture)
    {
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        auto picture = pdf::RenderClip(*SampleClip(), 2);
        EXPECT_EQ(80, picture.width);
        EXPECT_EQ(80, picture.height);
        auto decoded = DecodePng(picture.png);
        ASSERT_EQ(80u, decoded.width);
        EXPECT_GT(decoded.AlphaAt(40, 20), 200) << "the stroke at y 10 (20 pixels) is drawn";
        EXPECT_EQ(0, decoded.AlphaAt(40, 4)) << "around it the picture is transparent";
        if (SUCCEEDED(com)) CoUninitialize();
    }
}
