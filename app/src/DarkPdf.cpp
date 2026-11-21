#include "DarkPdf.h"

#include "DarkColors.h"
#include "xournal/base/Error.h"

#include <d2d1effects.h>
#include <algorithm>
#include <cmath>

namespace xournal::app::DarkPdf
{
    namespace
    {
        constexpr float Red = 0.2126f, Green = 0.7152f, Blue = 0.0722f;
        constexpr float Contrast = 1.80f;
        constexpr float Offset(unsigned channel) { return Contrast - 1 + channel / 255.0f; }

        class RestoreTransform
        {
        public:
            explicit RestoreTransform(ID2D1DeviceContext* dc) : dc(dc) { dc->GetTransform(&value); }
            ~RestoreTransform() { dc->SetTransform(value); }
            D2D1_MATRIX_3X2_F value{};

        private:
            ID2D1DeviceContext* dc;
        };
    }

    void Recolor(CpuBitmap& bitmap)
    {
        auto& pixels = bitmap.pixels;
        for (size_t i = 0; i + 3 < pixels.size(); i += 4)
        {
            float alpha = pixels[i + 3];
            float luma = Red * pixels[i + 2] + Green * pixels[i + 1] + Blue * pixels[i];
            float shift = -Contrast * luma;
            auto channel = [&](float value, unsigned sheet) {
                return static_cast<std::uint8_t>(std::lround(std::clamp(value + shift + Offset(sheet) * alpha, 0.0f, alpha)));
            };
            pixels[i] = channel(pixels[i], DarkColors::Sheet.b);
            pixels[i + 1] = channel(pixels[i + 1], DarkColors::Sheet.g);
            pixels[i + 2] = channel(pixels[i + 2], DarkColors::Sheet.r);
        }
    }

    void Drawing::Draw(ID2D1DeviceContext* dc, ID2D1Bitmap* bitmap, D2D1_RECT_F dest,
        D2D1_INTERPOLATION_MODE interpolation)
    {
        Microsoft::WRL::ComPtr<ID2D1Device> current;
        dc->GetDevice(&current);
        if (!matrix || current.Get() != device.Get())
        {
            Microsoft::WRL::ComPtr<ID2D1Effect> effect;
            Check("CreateEffect(ColorMatrix)", dc->CreateEffect(CLSID_D2D1ColorMatrix, &effect));
            constexpr float r = -Contrast * Red, g = -Contrast * Green, b = -Contrast * Blue;
            constexpr D2D1_MATRIX_5X4_F colors{
                1 + r, r, r, 0, g, 1 + g, g, 0, b, b, 1 + b, 0, 0, 0, 0, 1,
                Offset(DarkColors::Sheet.r), Offset(DarkColors::Sheet.g), Offset(DarkColors::Sheet.b), 0
            };
            Check("ColorMatrix: matrix", effect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, colors));
            Check("ColorMatrix: clamp", effect->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE));
            device = std::move(current);
            matrix = std::move(effect);
        }

        D2D1_SIZE_F size = bitmap->GetSize();
        RestoreTransform restore(dc);
        dc->SetTransform(D2D1::Matrix3x2F::Scale((dest.right - dest.left) / size.width,
            (dest.bottom - dest.top) / size.height) * D2D1::Matrix3x2F::Translation(dest.left, dest.top) * restore.value);
        matrix->SetInput(0, bitmap);
        dc->DrawImage(matrix.Get(), nullptr, nullptr, interpolation);
        matrix->SetInput(0, nullptr);
    }
}
