#include "xournal/model/XoppImage.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Text.h"
#include "xournal/model/XoppFormat.h"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>

namespace xournal::XoppImage
{
    std::optional<Size> NaturalSize(XmlElement const& e)
    {
        std::wstring text = e.AttributeOr(L"natural_size", L""); // the parts are views into it
        auto parts = Split(text, L' ');
        if (parts.size() != 2) return std::nullopt;
        auto w = XoppFormat::ReadNumber(parts[0]);
        auto h = XoppFormat::ReadNumber(parts[1]);
        if (!w || !h || *w <= 0 || *h <= 0) return std::nullopt;
        return Size{ *w, *h };
    }

    namespace
    {
        Rect LegacyBounds(XmlElement const& e)
        {
            double left = XoppFormat::ReadNumber(e, L"left", 0), top = XoppFormat::ReadNumber(e, L"top", 0);
            double right = XoppFormat::ReadNumber(e, L"right", 0), bottom = XoppFormat::ReadNumber(e, L"bottom", 0);
            return { left, top, std::max(0.0, right - left), std::max(0.0, bottom - top) };
        }

        std::wstring ToBase64(std::span<std::uint8_t const> data)
        {
            if (data.empty())
            {
                return {};
            }
            DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
            DWORD length = 0;
            if (!CryptBinaryToStringW(data.data(), static_cast<DWORD>(data.size()), flags, nullptr, &length))
            {
                ThrowLastError("CryptBinaryToStringW (size)");
            }
            std::wstring text(length, L'\0');
            if (!CryptBinaryToStringW(data.data(), static_cast<DWORD>(data.size()), flags, text.data(), &length))
            {
                ThrowLastError("CryptBinaryToStringW");
            }
            text.resize(length); // without the terminating zero
            return text;
        }

        void SetLegacyBounds(XmlElement& e, Rect bounds)
        {
            e.SetAttribute(L"left", XoppFormat::FormatNumber(bounds.Left()));
            e.SetAttribute(L"top", XoppFormat::FormatNumber(bounds.Top()));
            e.SetAttribute(L"right", XoppFormat::FormatNumber(bounds.Right()));
            e.SetAttribute(L"bottom", XoppFormat::FormatNumber(bounds.Bottom()));
        }
    }

    bool IsImage(XmlElement const& e) { return e.Name() == L"image" || e.Name() == L"teximage"; }

    XmlElementPtr Create(Rect bounds, std::span<std::uint8_t const> png, int pixelWidth, int pixelHeight)
    {
        auto e = XmlElement::Make(L"image");
        SetLegacyBounds(*e, bounds);
        e->SetAttribute(L"natural_size", std::format(L"{} {}", pixelWidth, pixelHeight));
        e->SetValue(ToBase64(png));
        return e;
    }

    Placement GetPlacement(XmlElement const& e)
    {
        auto m = XoppFormat::ReadMatrix(e);
        auto natural = NaturalSize(e);
        if (m && natural)
        {
            return { *natural, *m };
        }
        Rect b = LegacyBounds(e);
        return { { b.width, b.height }, Matrix::Translation(b.x, b.y) };
    }

    Rect GetBounds(XmlElement const& e)
    {
        auto m = XoppFormat::ReadMatrix(e);
        auto natural = NaturalSize(e);
        if (!m || !natural)
        {
            return LegacyBounds(e);
        }
        Point corners[] = { m->Apply({ 0, 0 }), m->Apply({ natural->width, 0 }),
            m->Apply({ natural->width, natural->height }), m->Apply({ 0, natural->height }) };
        Point low = corners[0], high = corners[0];
        for (auto const& c : corners)
        {
            low = { std::min(low.x, c.x), std::min(low.y, c.y) };
            high = { std::max(high.x, c.x), std::max(high.y, c.y) };
        }
        return Rect::FromCorners(low, high);
    }

    void SetBounds(XmlElement& e, Rect bounds)
    {
        auto m = XoppFormat::ReadMatrix(e);
        if (m && NaturalSize(e))
        {
            Rect old = GetBounds(e);
            if (old.width > 0 && old.height > 0)
            {
                Matrix map = Matrix::Translation(-old.x, -old.y) *
                    Matrix::Scale(bounds.width / old.width, bounds.height / old.height) *
                    Matrix::Translation(bounds.x, bounds.y);
                XoppFormat::WriteMatrix(e, *m * map);
                return;
            }
        }
        SetLegacyBounds(e, bounds);
    }

    void Transform(XmlElement& e, Matrix const& m, std::optional<Size> intrinsic)
    {
        if (auto matrix = XoppFormat::ReadMatrix(e))
        {
            XoppFormat::WriteMatrix(e, *matrix * m);
            return;
        }
        Rect b = LegacyBounds(e);
        if (!intrinsic)
        {
            SetLegacyBounds(e, Transform(b, m));
            return;
        }
        e.SetAttribute(L"natural_size", XoppFormat::FormatNumber(intrinsic->width, 6) + L" " +
            XoppFormat::FormatNumber(intrinsic->height, 6));
        XoppFormat::WriteMatrix(e, Matrix::Scale(b.width / intrinsic->width, b.height / intrinsic->height) *
            Matrix::Translation(b.x, b.y) * m);
    }

    Rect Transform(Rect bounds, Matrix const& m)
    {
        Point a = m.Apply({ bounds.Left(), bounds.Top() }), b = m.Apply({ bounds.Right(), bounds.Bottom() });
        Point c = m.Apply({ bounds.Right(), bounds.Top() }), d = m.Apply({ bounds.Left(), bounds.Bottom() });
        return Rect::FromCorners({ std::min({ a.x, b.x, c.x, d.x }), std::min({ a.y, b.y, c.y, d.y }) },
            { std::max({ a.x, b.x, c.x, d.x }), std::max({ a.y, b.y, c.y, d.y }) });
    }

    std::vector<std::uint8_t> GetData(XmlElement const& e)
    {
        std::wstring text(Trim(e.Value()));
        if (text.empty())
        {
            return {};
        }
        DWORD length = 0;
        if (!CryptStringToBinaryW(text.c_str(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &length,
                nullptr, nullptr))
        {
            LogError(std::format("picture data is not base64 ({}): shown empty", DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
            return {};
        }
        std::vector<std::uint8_t> data(length);
        if (!CryptStringToBinaryW(text.c_str(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, data.data(), &length,
                nullptr, nullptr))
        {
            ThrowLastError("CryptStringToBinaryW");
        }
        data.resize(length);
        return data;
    }

    bool IsPdf(std::span<std::uint8_t const> data)
    {
        return data.size() > 4 && data[0] == '%' && data[1] == 'P' && data[2] == 'D' && data[3] == 'F';
    }
}
