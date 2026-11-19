#include "AppearanceText.h"

#include "xournal/base/Log.h"
#include "xournal/model/Geometry.h"
#include "xournal/model/XoppFormat.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <stack>
#include <string>
#include <vector>

namespace xournal::import::detail
{
    namespace
    {
        Matrix Translation(double x, double y) { return Matrix::Translation(x, y); }

        // Follows the graphics and text state up to the first text-showing operator.
        class FirstLineFinder : public QPDFObjectHandle::ParserCallbacks
        {
        public:
            explicit FirstLineFinder(Matrix ctm) : ctm(ctm) {}
            std::optional<FirstLine> result;

            void handleObject(QPDFObjectHandle obj) override
            {
                if (!obj.isOperator())
                {
                    operands.push_back(obj);
                    return;
                }
                std::string op = obj.getOperatorValue();
                std::vector<double> n;
                for (auto& o : operands)
                {
                    if (o.isNumber()) n.push_back(o.getNumericValue());
                }
                operands.clear();
                if (op == "q") saved.push(ctm);
                else if (op == "Q" && !saved.empty()) { ctm = saved.top(); saved.pop(); }
                else if (op == "cm" && n.size() == 6) ctm = Matrix{ n[0], n[1], n[2], n[3], n[4], n[5] } * ctm;
                else if (op == "BT") tm = Matrix{};
                else if (op == "Tm" && n.size() == 6) tm = Matrix{ n[0], n[1], n[2], n[3], n[4], n[5] };
                else if ((op == "Td" || op == "TD") && n.size() == 2)
                {
                    tm = Translation(n[0], n[1]) * tm;
                    if (op == "TD") leading = -n[1];
                }
                else if (op == "TL" && n.size() == 1) leading = n[0];
                else if (op == "T*") tm = Translation(0, -leading) * tm;
                else if (op == "Tf" && n.size() == 1) fontSize = n[0];
                else if ((op == "Tj" || op == "TJ" || op == "'" || op == "\"") && fontSize > 0)
                {
                    if (op == "'" || op == "\"") tm = Translation(0, -leading) * tm; // these move to the next line first
                    Matrix m = tm * ctm;
                    double determinant = m.m11 * m.m22 - m.m12 * m.m21;
                    result = FirstLine{ m.m31, m.m32, std::round(fontSize * std::sqrt(std::abs(determinant)) * 10) / 10 };
                    terminateParsing();
                }
            }
            void handleEOF() override {}

        private:
            Matrix ctm;
            Matrix tm;
            std::stack<Matrix> saved;
            std::vector<QPDFObjectHandle> operands;
            double fontSize = 0, leading = 0;
        };
    }

    std::optional<FirstLine> FirstLineOf(QPDFObjectHandle annotation)
    {
        auto form = annotation.getKey("/AP").getKey("/N");
        if (!form.isStream() || !annotation.getKey("/Rect").isRectangle()) return std::nullopt;
        auto bboxObject = form.getDict().getKey("/BBox");
        if (!bboxObject.isRectangle()) return std::nullopt;
        auto box = bboxObject.getArrayAsRectangle();
        auto rect = annotation.getKey("/Rect").getArrayAsRectangle();
        double boxWidth = box.urx - box.llx, boxHeight = box.ury - box.lly;
        if (boxWidth == 0 || boxHeight == 0) return std::nullopt;
        double sx = std::abs((rect.urx - rect.llx) / boxWidth), sy = std::abs((rect.ury - rect.lly) / boxHeight);
        Matrix ctm{ sx, 0, 0, sy, std::min(rect.llx, rect.urx) - sx * std::min(box.llx, box.urx),
                    std::min(rect.lly, rect.ury) - sy * std::min(box.lly, box.ury) };
        FirstLineFinder finder(ctm);
        try
        {
            form.parseAsContents(&finder);
        }
        catch (std::exception const&)
        {
            LogException("import: text box appearance; placed at its rectangle");
            return std::nullopt;
        }
        return finder.result;
    }

    DefaultAppearance ParseDefaultAppearance(std::string_view da)
    {
        std::vector<std::string_view> words;
        for (size_t i = 0; i < da.size();)
        {
            while (i < da.size() && da[i] == ' ') ++i;
            size_t start = i;
            while (i < da.size() && da[i] != ' ') ++i;
            if (i > start) words.push_back(da.substr(start, i - start));
        }
        auto number = [](std::string_view s) { return XoppFormat::ParseDouble(std::wstring(s.begin(), s.end())); };
        auto byte = [&](std::string_view s) {
            auto v = number(s);
            return v ? static_cast<std::uint8_t>(std::lround(std::clamp(*v, 0.0, 1.0) * 255)) : std::uint8_t{ 0 };
        };
        DefaultAppearance result;
        for (size_t i = 0; i < words.size(); ++i)
        {
            if (words[i] == "Tf" && i >= 1)
            {
                if (auto s = number(words[i - 1]); s && *s > 0) result.size = *s;
            }
            else if (words[i] == "g" && i >= 1)
            {
                auto gray = byte(words[i - 1]);
                result.color = Color{ 255, gray, gray, gray };
            }
            else if (words[i] == "rg" && i >= 3)
            {
                result.color = Color{ 255, byte(words[i - 3]), byte(words[i - 2]), byte(words[i - 1]) };
            }
        }
        return result;
    }
}
