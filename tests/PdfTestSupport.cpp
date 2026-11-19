#include "PdfTestSupport.h"

#include "xournal/base/Error.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <gtest/gtest.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <fstream>
#include <iterator>
#include <regex>
#include <set>

using Microsoft::WRL::ComPtr;

namespace xournal::tests
{
    namespace
    {
        std::vector<std::uint8_t> Encode(GUID const& container, unsigned width, unsigned height, bool withAlpha)
        {
            HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED); // stays initialized for the test process
            if (com != RPC_E_CHANGED_MODE) Check("CoInitializeEx", com);
            ComPtr<IWICImagingFactory> wic;
            Check("WIC", CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
            ComPtr<IStream> stream;
            Check("CreateStreamOnHGlobal", CreateStreamOnHGlobal(nullptr, TRUE, &stream));
            ComPtr<IWICBitmapEncoder> encoder;
            Check("CreateEncoder", wic->CreateEncoder(container, nullptr, &encoder));
            Check("Initialize", encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
            ComPtr<IWICBitmapFrameEncode> frame;
            Check("CreateNewFrame", encoder->CreateNewFrame(&frame, nullptr));
            Check("frame Initialize", frame->Initialize(nullptr));
            Check("SetSize", frame->SetSize(width, height));
            bool jpeg = container == GUID_ContainerFormatJpeg;
            WICPixelFormatGUID format = jpeg ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
            Check("SetPixelFormat", frame->SetPixelFormat(&format));
            unsigned bpp = jpeg ? 3 : 4;
            std::vector<BYTE> pixels(static_cast<size_t>(width) * height * bpp);
            for (unsigned y = 0; y < height; ++y)
            {
                for (unsigned x = 0; x < width; ++x)
                {
                    BYTE* p = &pixels[(static_cast<size_t>(y) * width + x) * bpp];
                    p[0] = static_cast<BYTE>(x * 255 / width);
                    p[1] = static_cast<BYTE>(y * 255 / height);
                    p[2] = 0x80;
                    if (!jpeg) p[3] = withAlpha && x >= width / 2 ? 0x60 : 0xFF;
                }
            }
            Check("WritePixels", frame->WritePixels(height, width * bpp, static_cast<UINT>(pixels.size()), pixels.data()));
            Check("frame Commit", frame->Commit());
            Check("encoder Commit", encoder->Commit());
            STATSTG stat{};
            Check("Stat", stream->Stat(&stat, STATFLAG_NONAME));
            std::vector<std::uint8_t> data(static_cast<size_t>(stat.cbSize.QuadPart));
            LARGE_INTEGER zero{};
            Check("Seek", stream->Seek(zero, STREAM_SEEK_SET, nullptr));
            ULONG read = 0;
            Check("Read", stream->Read(data.data(), static_cast<ULONG>(data.size()), &read));
            return data;
        }

        QPDFObjectHandle Name(char const* n) { return QPDFObjectHandle::newName(n); }

        QPDFObjectHandle Appearance(QPDF& pdf, std::string const& token, QPDFObjectHandle font)
        {
            auto ap = pdf.newStream("1 0 0 RG 2 w 2 2 156 46 re S BT /F1 18 Tf 1 0 0 rg 12 18 Td (" + token + ") Tj ET\n");
            auto d = ap.getDict();
            d.replaceKey("/Type", Name("/XObject"));
            d.replaceKey("/Subtype", Name("/Form"));
            d.replaceKey("/BBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0, 0, 160, 50)));
            auto fonts = QPDFObjectHandle::newDictionary();
            fonts.replaceKey("/F1", font);
            auto res = QPDFObjectHandle::newDictionary();
            res.replaceKey("/Font", fonts);
            d.replaceKey("/Resources", res);
            return ap;
        }

        QPDFObjectHandle Annotation(QPDF& pdf, char const* subtype, QPDFObjectHandle::Rectangle rect, int flags, QPDFObjectHandle appearance)
        {
            auto a = QPDFObjectHandle::newDictionary();
            a.replaceKey("/Type", Name("/Annot"));
            a.replaceKey("/Subtype", Name(subtype));
            a.replaceKey("/Rect", QPDFObjectHandle::newArray(rect));
            a.replaceKey("/F", QPDFObjectHandle::newInteger(flags));
            if (appearance.isInitialized())
            {
                auto ap = QPDFObjectHandle::newDictionary();
                ap.replaceKey("/N", appearance);
                a.replaceKey("/AP", ap);
            }
            return pdf.makeIndirectObject(a);
        }

        std::map<int, std::wstring> ToUnicodeOf(QPDFObjectHandle font)
        {
            std::map<int, std::wstring> map;
            auto tu = font.getKey("/ToUnicode");
            if (!tu.isStream()) return map;
            auto data = tu.getStreamData();
            std::string text(reinterpret_cast<char const*>(data->getBuffer()), data->getSize());
            std::regex entry("<([0-9A-Fa-f]{4})>\\s*<([0-9A-Fa-f]+)>");
            for (auto it = std::sregex_iterator(text.begin(), text.end(), entry); it != std::sregex_iterator(); ++it)
            {
                int code = std::stoi((*it)[1].str(), nullptr, 16);
                std::string hex = (*it)[2].str();
                std::wstring chars;
                for (size_t i = 0; i + 4 <= hex.size(); i += 4) chars += static_cast<wchar_t>(std::stoi(hex.substr(i, 4), nullptr, 16));
                map[code] = chars;
            }
            return map;
        }

        class Collector : public QPDFObjectHandle::ParserCallbacks
        {
        public:
            Collector(PageContents& out, QPDFObjectHandle resources, std::set<QPDFObjGen>& seen)
                : out(out), resources(resources), seen(seen) {}

            void handleObject(QPDFObjectHandle obj) override
            {
                if (!obj.isOperator())
                {
                    operands.push_back(obj);
                    return;
                }
                std::string op = obj.getOperatorValue();
                out.operators[op]++;
                std::string text;
                for (auto& o : operands) text += o.unparse() + " ";
                out.operatorLines.emplace_back(op, text);
                if (op == "Tf" && !operands.empty() && operands[0].isName()) Font(operands[0].getName());
                if ((op == "Tj" || op == "'" || op == "\"") && !operands.empty()) Show(operands.back());
                if (op == "TJ" && !operands.empty() && operands.back().isArray())
                {
                    for (auto& item : operands.back().aitems())
                    {
                        if (item.isString()) Show(item);
                    }
                }
                if (op == "ET") out.text += "\n";
                if (op == "Do" && !operands.empty() && operands[0].isName()) Do(operands[0].getName());
                operands.clear();
            }
            void handleEOF() override {}

        private:
            void Font(std::string const& name)
            {
                auto font = resources.getKey("/Font").getKey(name);
                type0 = font.getKey("/Subtype").isNameAndEquals("/Type0");
                unicode = type0 ? ToUnicodeOf(font) : std::map<int, std::wstring>{};
            }
            void Show(QPDFObjectHandle s)
            {
                std::string bytes = s.getStringValue();
                if (!type0)
                {
                    out.text += bytes;
                    return;
                }
                std::wstring chars;
                for (size_t i = 0; i + 1 < bytes.size(); i += 2)
                {
                    int code = static_cast<unsigned char>(bytes[i]) << 8 | static_cast<unsigned char>(bytes[i + 1]);
                    auto it = unicode.find(code);
                    if (it != unicode.end()) chars += it->second;
                }
                out.text += ToUtf8(chars);
            }
            void Do(std::string const& name)
            {
                auto xo = resources.getKey("/XObject").getKey(name);
                if (!xo.isStream()) return;
                auto dict = xo.getDict();
                if (dict.getKey("/Subtype").isNameAndEquals("/Image"))
                {
                    ImageInfo info;
                    auto filter = dict.getKey("/Filter");
                    info.filter = filter.isName() ? filter.getName() : (filter.isArray() ? filter.unparse() : "");
                    auto raw = xo.getRawStreamData();
                    info.rawData.assign(reinterpret_cast<char const*>(raw->getBuffer()), raw->getSize());
                    info.softMask = dict.hasKey("/SMask");
                    info.id = xo.getObjGen();
                    out.images.push_back(info);
                }
                else if (dict.getKey("/Subtype").isNameAndEquals("/Form") && seen.insert(xo.getObjGen()).second)
                {
                    out.forms++;
                    Collector inner(out, dict.getKey("/Resources"), seen);
                    xo.parseAsContents(&inner);
                    seen.erase(xo.getObjGen());
                }
            }

            PageContents& out;
            QPDFObjectHandle resources;
            std::set<QPDFObjGen>& seen;
            std::vector<QPDFObjectHandle> operands;
            bool type0 = false;
            std::map<int, std::wstring> unicode;
        };
    }

    std::vector<std::uint8_t> MakePng(unsigned width, unsigned height, bool withAlpha)
    {
        return Encode(GUID_ContainerFormatPng, width, height, withAlpha);
    }

    std::vector<std::uint8_t> MakeJpeg(unsigned width, unsigned height)
    {
        return Encode(GUID_ContainerFormatJpeg, width, height, false);
    }

    std::vector<std::uint8_t> ReadBytes(std::filesystem::path const& file)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open " + ToUtf8(file.wstring()));
        return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    }

    SourcePdf WriteSourcePdf(std::filesystem::path const& file, std::vector<std::string> const& pageTexts)
    {
        SourcePdf source;
        source.jpeg = MakeJpeg(64, 48);
        QPDF pdf;
        pdf.emptyPDF();

        auto font = QPDFObjectHandle::newDictionary();
        font.replaceKey("/Type", Name("/Font"));
        font.replaceKey("/Subtype", Name("/Type1"));
        font.replaceKey("/BaseFont", Name("/Helvetica"));
        font.replaceKey("/Encoding", Name("/WinAnsiEncoding"));
        font = pdf.makeIndirectObject(font);

        auto image = pdf.newStream();
        image.replaceStreamData(std::string(source.jpeg.begin(), source.jpeg.end()), Name("/DCTDecode"), QPDFObjectHandle::newNull());
        auto id = image.getDict();
        id.replaceKey("/Type", Name("/XObject"));
        id.replaceKey("/Subtype", Name("/Image"));
        id.replaceKey("/Width", QPDFObjectHandle::newInteger(64));
        id.replaceKey("/Height", QPDFObjectHandle::newInteger(48));
        id.replaceKey("/ColorSpace", Name("/DeviceRGB"));
        id.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));

        auto makePage = [&](double w, double h, std::string const& content, bool withImage) {
            auto fonts = QPDFObjectHandle::newDictionary();
            fonts.replaceKey("/F1", font);
            auto res = QPDFObjectHandle::newDictionary();
            res.replaceKey("/Font", fonts);
            if (withImage)
            {
                auto xo = QPDFObjectHandle::newDictionary();
                xo.replaceKey("/Im1", image);
                res.replaceKey("/XObject", xo);
            }
            auto page = QPDFObjectHandle::newDictionary();
            page.replaceKey("/Type", Name("/Page"));
            page.replaceKey("/MediaBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0, 0, w, h)));
            page.replaceKey("/Resources", res);
            page.replaceKey("/Contents", pdf.newStream(content));
            return pdf.makeIndirectObject(page);
        };

        auto page1 = makePage(595, 842, "BT /F1 24 Tf 72 760 Td (BACKGROUNDTEXT page one) Tj ET\nq 128 0 0 96 72 600 cm /Im1 Do Q\n", true);
        auto annots = QPDFObjectHandle::newArray();
        annots.appendItem(Annotation(pdf, "/Stamp", { 350, 300, 510, 350 }, 4, Appearance(pdf, "STAMPTOKEN", font)));
        annots.appendItem(Annotation(pdf, "/Stamp", { 350, 200, 510, 250 }, 2 | 4, Appearance(pdf, "HIDDENTOKEN", font)));
        annots.appendItem(Annotation(pdf, "/Link", { 72, 100, 200, 120 }, 4, QPDFObjectHandle()));
        page1.replaceKey("/Annots", annots);
        pdf.addPage(page1, false);
        pdf.addPage(makePage(420, 298, "BT /F1 18 Tf 40 250 Td (SECONDPAGE) Tj ET\n", false), false);
        for (auto const& text : pageTexts)
        {
            pdf.addPage(makePage(595, 842, "BT /F1 18 Tf 40 700 Td (" + text + ") Tj ET\n", false), false);
        }

        auto info = QPDFObjectHandle::newDictionary();
        info.replaceKey("/Author", QPDFObjectHandle::newString("Secret Person"));
        pdf.getTrailer().replaceKey("/Info", pdf.makeIndirectObject(info));

        std::string utf8 = ToUtf8(Paths::Full(file).wstring());
        QPDFWriter writer(pdf, utf8.c_str());
        writer.write();
        return source;
    }

    std::vector<std::string> PageContents::OperandsOf(std::string const& op) const
    {
        std::vector<std::string> result;
        for (auto const& [o, operands] : operatorLines)
        {
            if (o == op) result.push_back(operands);
        }
        return result;
    }

    PageContents Inspect(QPDFPageObjectHelper page)
    {
        PageContents out;
        std::set<QPDFObjGen> seen;
        Collector collector(out, page.getAttribute("/Resources", false), seen);
        page.parseContents(&collector);
        return out;
    }

    OpenPdf Open(std::filesystem::path const& file)
    {
        OpenPdf open;
        open.pdf = std::make_shared<QPDF>();
        std::string utf8 = ToUtf8(Paths::Full(file).wstring());
        open.pdf->processFile(utf8.c_str());
        open.pages = QPDFPageDocumentHelper(*open.pdf).getAllPages();
        for (auto const& w : open.pdf->getWarnings()) ADD_FAILURE() << "qpdf warning reading " << utf8 << ": " << w.what();
        return open;
    }

    namespace
    {
        unsigned U16(std::string const& f, size_t at) { return static_cast<unsigned char>(f[at]) << 8 | static_cast<unsigned char>(f[at + 1]); }
        unsigned U32(std::string const& f, size_t at) { return U16(f, at) << 16 | U16(f, at + 2); }
    }

    void KeepForInspection(std::filesystem::path const& file, std::wstring const& name)
    {
        wchar_t folder[MAX_PATH] = L"";
        if (GetEnvironmentVariableW(L"XOURNAL_PDF_TEST_OUTPUT", folder, MAX_PATH) == 0) return;
        std::filesystem::create_directories(folder);
        std::filesystem::copy_file(file, std::filesystem::path(folder) / name, std::filesystem::copy_options::overwrite_existing);
    }

    int NumGlyphs(std::string const& font)
    {
        unsigned tables = U16(font, 4);
        for (unsigned i = 0; i < tables; ++i)
        {
            size_t record = 12 + static_cast<size_t>(i) * 16;
            if (font.compare(record, 4, "maxp") == 0) return static_cast<int>(U16(font, U32(font, record + 8) + 4));
        }
        return -1;
    }

    std::vector<std::string> TableTags(std::string const& font)
    {
        std::vector<std::string> tags;
        unsigned tables = U16(font, 4);
        for (unsigned i = 0; i < tables; ++i) tags.push_back(font.substr(12 + static_cast<size_t>(i) * 16, 4));
        return tags;
    }
}
