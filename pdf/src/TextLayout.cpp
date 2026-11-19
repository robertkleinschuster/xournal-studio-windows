#include "TextLayout.h"

#include "xournal/pdf/TextBoxLayout.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"

#include <wrl/implements.h>

#include <algorithm>
#include <format>

using Microsoft::WRL::ComPtr;

namespace xournal::pdf::detail
{
    namespace
    {
        // Receives the layout's glyph runs (IDWriteTextLayout::Draw) instead of rendering them.
        class RunCollector
            : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDWriteTextRenderer>
        {
        public:
            std::vector<GlyphRun> runs;

            STDMETHODIMP IsPixelSnappingDisabled(void*, BOOL* disabled) override
            {
                *disabled = TRUE; // positions as designed (no device)
                return S_OK;
            }
            STDMETHODIMP GetCurrentTransform(void*, DWRITE_MATRIX* transform) override
            {
                *transform = DWRITE_MATRIX{ 1, 0, 0, 1, 0, 0 };
                return S_OK;
            }
            STDMETHODIMP GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override
            {
                *pixelsPerDip = 1;
                return S_OK;
            }

            STDMETHODIMP DrawGlyphRun(void*, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE, DWRITE_GLYPH_RUN const* run,
                                      DWRITE_GLYPH_RUN_DESCRIPTION const* description, IUnknown*) override
            {
                GlyphRun out;
                out.face = run->fontFace;
                out.emSize = run->fontEmSize;
                out.sideways = run->isSideways != FALSE;
                out.rightToLeft = (run->bidiLevel & 1) != 0;
                out.origin = { x, y };
                out.glyphs.assign(run->glyphIndices, run->glyphIndices + run->glyphCount);
                out.advances.assign(run->glyphAdvances, run->glyphAdvances + run->glyphCount);
                if (run->glyphOffsets && std::any_of(run->glyphOffsets, run->glyphOffsets + run->glyphCount,
                                                     [](DWRITE_GLYPH_OFFSET const& o) { return o.advanceOffset != 0 || o.ascenderOffset != 0; }))
                {
                    out.offsets.assign(run->glyphOffsets, run->glyphOffsets + run->glyphCount);
                }
                out.glyphText.resize(run->glyphCount);
                // Cluster map: character i belongs to the cluster starting at glyph clusterMap[i]; a cluster's characters
                // are given to its first glyph (for ToUnicode).
                if (description && description->string && description->clusterMap)
                {
                    for (UINT32 i = 0; i < description->stringLength; ++i)
                    {
                        UINT16 g = description->clusterMap[i];
                        if (g < out.glyphText.size()) out.glyphText[g] += description->string[i];
                    }
                }
                runs.push_back(std::move(out));
                return S_OK;
            }
            // .xopp texts have no underline, strikethrough or inline objects; none are set on the layout.
            STDMETHODIMP DrawUnderline(void*, FLOAT, FLOAT, DWRITE_UNDERLINE const*, IUnknown*) override { return S_OK; }
            STDMETHODIMP DrawStrikethrough(void*, FLOAT, FLOAT, DWRITE_STRIKETHROUGH const*, IUnknown*) override { return S_OK; }
            STDMETHODIMP DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return E_NOTIMPL; }
        };
    }

    TextLayout::TextLayout()
    {
        Check("DWriteCreateFactory", DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                         reinterpret_cast<IUnknown**>(factory.GetAddressOf())));
        Check("GetSystemFontCollection", factory->GetSystemFontCollection(&systemFonts, FALSE));
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = L"";
        if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) == 0)
        {
            Log(std::format("pdf export: GetUserDefaultLocaleName failed ({}); en-us for text layout", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
            locale = L"en-us";
        }
        else
        {
            locale = name;
        }
    }

    std::vector<GlyphRun> TextLayout::Layout(XoppText const& text)
    {
        if (text.content.empty())
        {
            return {};
        }
        std::wstring family = text.WindowsFamily();
        if (family.empty()) family = L"Arial";
        UINT32 index = 0;
        BOOL exists = FALSE;
        Check("FindFamilyName", systemFonts->FindFamilyName(family.c_str(), &index, &exists));
        if (!exists && reportedMissing.insert(family).second)
        {
            Log(std::format("pdf export: font family '{}' is not installed; DirectWrite's fallback is used", ToUtf8(family)));
        }

        auto layout = TextBoxLayout(factory.Get(), text, family, locale);

        auto collector = Microsoft::WRL::Make<RunCollector>();
        Check("IDWriteTextLayout::Draw", layout->Draw(nullptr, collector.Get(), 0, 0));
        return std::move(collector->runs);
    }
}
