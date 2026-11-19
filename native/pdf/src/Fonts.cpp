#include "Fonts.h"

#include "FontTables.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <fontsub.h>

#include <algorithm>
#include <cstdlib>
#include <format>

using Microsoft::WRL::ComPtr;

namespace xournal::pdf::detail
{
    namespace
    {
        // An OpenType table of the face; empty when the face has none.
        std::vector<std::uint8_t> Table(IDWriteFontFace* face, UINT32 tag)
        {
            void const* data = nullptr;
            UINT32 size = 0;
            void* context = nullptr;
            BOOL exists = FALSE;
            Check("TryGetFontTable", face->TryGetFontTable(tag, &data, &size, &context, &exists));
            std::vector<std::uint8_t> table;
            if (exists)
            {
                table.assign(static_cast<std::uint8_t const*>(data), static_cast<std::uint8_t const*>(data) + size);
                face->ReleaseFontTable(context);
            }
            return table;
        }

        std::uint16_t U16(std::vector<std::uint8_t> const& t, size_t at)
        {
            return at + 1 < t.size() ? static_cast<std::uint16_t>(t[at] << 8 | t[at + 1]) : 0;
        }

        std::wstring InformationalString(IDWriteFontFace* face, DWRITE_INFORMATIONAL_STRING_ID id)
        {
            ComPtr<IDWriteFontFace3> face3;
            Check("IDWriteFontFace3", face->QueryInterface(IID_PPV_ARGS(&face3)));
            ComPtr<IDWriteLocalizedStrings> strings;
            BOOL exists = FALSE;
            Check("GetInformationalStrings", face3->GetInformationalStrings(id, &strings, &exists));
            if (!exists || !strings || strings->GetCount() == 0)
            {
                return {};
            }
            UINT32 length = 0;
            Check("GetStringLength", strings->GetStringLength(0, &length));
            std::wstring s(length + 1, L'\0');
            Check("GetString", strings->GetString(0, s.data(), length + 1));
            s.resize(length);
            return s;
        }

        std::string Describe(IDWriteFontFace* face)
        {
            std::wstring name = InformationalString(face, DWRITE_INFORMATIONAL_STRING_FULL_NAME);
            return name.empty() ? std::string("(unnamed font)") : ToUtf8(name);
        }

        // A PDF name token: printable ASCII without delimiters.
        std::string NameToken(std::wstring const& name)
        {
            std::string out;
            for (wchar_t c : name)
            {
                if (c > 32 && c < 127 && !wcschr(L"()<>[]{}/%#", c)) out += static_cast<char>(c);
            }
            return out;
        }

        std::string Hex4(unsigned value) { return std::format("{:04X}", value & 0xFFFF); }

        void* CFP_Allocate(size_t size) { return std::malloc(size); }
        void* CFP_Reallocate(void* p, size_t size) { return std::realloc(p, size); }
        void CFP_Free(void* p) { std::free(p); }
    }

    std::vector<std::uint8_t> FontFileData(IDWriteFontFace* face)
    {
        UINT32 count = 0;
        Check("IDWriteFontFace::GetFiles", face->GetFiles(&count, nullptr));
        if (count != 1)
        {
            ThrowInvalidData(std::format("font face with {} files", count));
        }
        ComPtr<IDWriteFontFile> file;
        Check("IDWriteFontFace::GetFiles", face->GetFiles(&count, &file));
        void const* key = nullptr;
        UINT32 keySize = 0;
        Check("GetReferenceKey", file->GetReferenceKey(&key, &keySize));
        ComPtr<IDWriteFontFileLoader> loader;
        Check("GetLoader", file->GetLoader(&loader));
        ComPtr<IDWriteFontFileStream> stream;
        Check("CreateStreamFromKey", loader->CreateStreamFromKey(key, keySize, &stream));
        UINT64 size = 0;
        Check("GetFileSize", stream->GetFileSize(&size));
        void const* fragment = nullptr;
        void* context = nullptr;
        Check("ReadFileFragment", stream->ReadFileFragment(&fragment, 0, size, &context));
        std::vector<std::uint8_t> data(static_cast<std::uint8_t const*>(fragment), static_cast<std::uint8_t const*>(fragment) + size);
        stream->ReleaseFileFragment(context);
        return data;
    }

    std::optional<std::string> WhyNotEmbeddable(IDWriteFontFace* face)
    {
        auto type = face->GetType();
        if (type != DWRITE_FONT_FACE_TYPE_TRUETYPE && type != DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION)
        {
            return std::format("outline format {} (not TrueType)", static_cast<int>(type));
        }
        if (face->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE)
        {
            return "simulated bold/oblique";
        }
        auto os2 = Table(face, DWRITE_MAKE_OPENTYPE_TAG('O', 'S', '/', '2'));
        if (os2.size() < 10)
        {
            return "no OS/2 table (embedding rights unknown)";
        }
        std::uint16_t fsType = U16(os2, 8);
        if ((fsType & 0x000F) == 0x0002) return "license: restricted (fsType 0x0002)";
        if (fsType & 0x0200) return "license: bitmap embedding only (fsType 0x0200)";
        return std::nullopt;
    }

    std::string Fonts::KeyOf(IDWriteFontFace* face) const
    {
        auto cached = keys.find(face);
        if (cached != keys.end())
        {
            return cached->second;
        }
        UINT32 count = 0;
        Check("IDWriteFontFace::GetFiles", face->GetFiles(&count, nullptr));
        std::vector<ComPtr<IDWriteFontFile>> files(count);
        std::vector<IDWriteFontFile*> raw(count, nullptr);
        Check("IDWriteFontFace::GetFiles", face->GetFiles(&count, raw.data()));
        for (UINT32 i = 0; i < count; ++i) files[i].Attach(raw[i]);
        std::string key;
        for (auto const& file : files)
        {
            void const* ref = nullptr;
            UINT32 size = 0;
            Check("GetReferenceKey", file->GetReferenceKey(&ref, &size));
            ComPtr<IDWriteFontFileLoader> loader;
            Check("GetLoader", file->GetLoader(&loader));
            key += std::format("{}:", static_cast<void const*>(loader.Get()));
            key.append(static_cast<char const*>(ref), size);
        }
        key += std::format("#{}s{}", face->GetIndex(), static_cast<int>(face->GetSimulations()));
        keys.emplace(face, key);
        return key;
    }

    void Fonts::Collect(GlyphRun const& run)
    {
        auto [it, added] = faces.try_emplace(KeyOf(run.face.Get()));
        if (added) it->second.face = run.face;
        for (size_t i = 0; i < run.glyphs.size(); ++i)
        {
            auto& text = it->second.glyphs[run.glyphs[i]];
            if (text.empty()) text = run.glyphText[i];
        }
    }

    void Fonts::Build()
    {
        size_t number = 0;
        for (auto& [key, face] : faces)
        {
            ++number;
            if (auto why = WhyNotEmbeddable(face.face.Get()))
            {
                Log(std::format("pdf export: font {} drawn as outlines: {}", Describe(face.face.Get()), *why));
                continue;
            }
            try
            {
                face.font = Embed(face, number);
            }
            catch (Error const&)
            {
                LogException(std::format("pdf export: font {} drawn as outlines", Describe(face.face.Get())));
            }
        }
    }

    EmbeddedFont const* Fonts::For(IDWriteFontFace* face) const
    {
        auto it = faces.find(KeyOf(face));
        return it == faces.end() || !it->second.font ? nullptr : &*it->second.font;
    }

    EmbeddedFont Fonts::Embed(Face& entry, size_t number)
    {
        IDWriteFontFace* face = entry.face.Get();
        std::vector<std::uint8_t> source = FontFileData(face);

        std::vector<unsigned short> keep{ 0 }; // .notdef always
        for (auto const& [glyph, text] : entry.glyphs)
        {
            if (glyph != 0) keep.push_back(glyph);
        }
        bool collection = face->GetType() == DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION;
        unsigned char* subset = nullptr;
        unsigned long allocated = 0, written = 0;
        unsigned long error = CreateFontPackage(
            source.data(), static_cast<unsigned long>(source.size()), &subset, &allocated, &written,
            static_cast<unsigned short>(TTFCFP_FLAGS_SUBSET | TTFCFP_FLAGS_GLYPHLIST | (collection ? TTFCFP_FLAGS_TTC : 0)),
            static_cast<unsigned short>(face->GetIndex()), TTFCFP_SUBSET, 0, TTFCFP_MS_PLATFORMID, TTFCFP_DONT_CARE,
            keep.data(), static_cast<unsigned short>(keep.size()), CFP_Allocate, CFP_Reallocate, CFP_Free, nullptr);
        if (error != 0)
        {
            CFP_Free(subset);
            ThrowHr(std::format("CreateFontPackage (error {})", error), E_FAIL);
        }
        std::string fontFile = KeepPdfTables(std::string_view(reinterpret_cast<char const*>(subset), written));
        CFP_Free(subset);

        DWRITE_FONT_METRICS1 metrics{};
        ComPtr<IDWriteFontFace1> face1;
        Check("IDWriteFontFace1", face->QueryInterface(IID_PPV_ARGS(&face1)));
        face1->GetMetrics(&metrics);
        double const scale = 1000.0 / metrics.designUnitsPerEm;

        std::vector<UINT16> glyphs;
        for (auto const& [glyph, text] : entry.glyphs) glyphs.push_back(glyph);
        std::vector<DWRITE_GLYPH_METRICS> glyphMetrics(glyphs.size());
        Check("GetDesignGlyphMetrics", face->GetDesignGlyphMetrics(glyphs.data(), static_cast<UINT32>(glyphs.size()), glyphMetrics.data(), FALSE));
        EmbeddedFont font;
        for (size_t i = 0; i < glyphs.size(); ++i)
        {
            font.widths[glyphs[i]] = glyphMetrics[i].advanceWidth * scale;
        }

        // Subset tag (ISO 32000-1, 9.6.4): six capitals derived from the content - the output stays reproducible.
        std::string baseName = NameToken(InformationalString(face, DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME));
        if (baseName.empty()) baseName = std::format("Font{}", number);
        std::uint32_t hash = 2166136261u;
        for (UINT16 g : glyphs) hash = (hash ^ g) * 16777619u;
        for (char c : baseName) hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
        std::string tag;
        for (int i = 0; i < 6; ++i, hash /= 26) tag += static_cast<char>('A' + hash % 26);
        std::string fontName = "/" + tag + "+" + baseName;

        auto file = pdf.newStream(fontFile);
        file.getDict().replaceKey("/Length1", QPDFObjectHandle::newInteger(static_cast<long long>(fontFile.size())));

        auto post = Table(face, DWRITE_MAKE_OPENTYPE_TAG('p', 'o', 's', 't'));
        double italicAngle = post.size() >= 8 ? static_cast<std::int16_t>(U16(post, 4)) + U16(post, 6) / 65536.0 : 0.0;

        auto descriptor = QPDFObjectHandle::newDictionary();
        descriptor.replaceKey("/Type", QPDFObjectHandle::newName("/FontDescriptor"));
        descriptor.replaceKey("/FontName", QPDFObjectHandle::newName(fontName));
        descriptor.replaceKey("/Flags", QPDFObjectHandle::newInteger(italicAngle != 0 ? 32 | 64 : 32)); // nonsymbolic
        descriptor.replaceKey("/FontBBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(
            metrics.glyphBoxLeft * scale, metrics.glyphBoxBottom * scale, metrics.glyphBoxRight * scale, metrics.glyphBoxTop * scale)));
        descriptor.replaceKey("/ItalicAngle", QPDFObjectHandle::newReal(italicAngle, 2));
        descriptor.replaceKey("/Ascent", QPDFObjectHandle::newReal(metrics.ascent * scale, 1));
        descriptor.replaceKey("/Descent", QPDFObjectHandle::newReal(-static_cast<double>(metrics.descent) * scale, 1));
        descriptor.replaceKey("/CapHeight", QPDFObjectHandle::newReal(metrics.capHeight * scale, 1));
        descriptor.replaceKey("/StemV", QPDFObjectHandle::newInteger(80)); // required; not in TrueType data
        descriptor.replaceKey("/FontFile2", file);

        auto widths = QPDFObjectHandle::newArray();
        for (auto const& [glyph, width] : font.widths)
        {
            widths.appendItem(QPDFObjectHandle::newInteger(glyph));
            auto w = QPDFObjectHandle::newArray();
            w.appendItem(QPDFObjectHandle::newReal(width, 1));
            widths.appendItem(w);
        }

        auto cidSystemInfo = QPDFObjectHandle::newDictionary();
        cidSystemInfo.replaceKey("/Registry", QPDFObjectHandle::newString("Adobe"));
        cidSystemInfo.replaceKey("/Ordering", QPDFObjectHandle::newString("Identity"));
        cidSystemInfo.replaceKey("/Supplement", QPDFObjectHandle::newInteger(0));

        auto cidFont = QPDFObjectHandle::newDictionary();
        cidFont.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
        cidFont.replaceKey("/Subtype", QPDFObjectHandle::newName("/CIDFontType2"));
        cidFont.replaceKey("/BaseFont", QPDFObjectHandle::newName(fontName));
        cidFont.replaceKey("/CIDSystemInfo", cidSystemInfo);
        cidFont.replaceKey("/FontDescriptor", pdf.makeIndirectObject(descriptor));
        cidFont.replaceKey("/W", widths);
        cidFont.replaceKey("/CIDToGIDMap", QPDFObjectHandle::newName("/Identity"));

        // ToUnicode: glyph id -> the characters it stands for (search, copy).
        std::string cmap =
            "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
            "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
            "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
        std::vector<std::pair<UINT16, std::wstring>> mapped;
        for (auto const& [glyph, text] : entry.glyphs)
        {
            if (!text.empty()) mapped.emplace_back(glyph, text);
        }
        for (size_t start = 0; start < mapped.size(); start += 100)
        {
            size_t end = std::min(mapped.size(), start + 100);
            cmap += std::format("{} beginbfchar\n", end - start);
            for (size_t i = start; i < end; ++i)
            {
                cmap += "<" + Hex4(mapped[i].first) + "> <";
                for (wchar_t c : mapped[i].second) cmap += Hex4(c);
                cmap += ">\n";
            }
            cmap += "endbfchar\n";
        }
        cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";

        auto type0 = QPDFObjectHandle::newDictionary();
        type0.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
        type0.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type0"));
        type0.replaceKey("/BaseFont", QPDFObjectHandle::newName(fontName));
        type0.replaceKey("/Encoding", QPDFObjectHandle::newName("/Identity-H"));
        auto descendants = QPDFObjectHandle::newArray();
        descendants.appendItem(pdf.makeIndirectObject(cidFont));
        type0.replaceKey("/DescendantFonts", descendants);
        type0.replaceKey("/ToUnicode", pdf.newStream(cmap));
        font.dictionary = pdf.makeIndirectObject(type0);
        return font;
    }
}
