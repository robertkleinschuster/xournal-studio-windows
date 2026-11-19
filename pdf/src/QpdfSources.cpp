#include "QpdfSources.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDFMatrix.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include <algorithm>
#include <cwctype>
#include <format>
#include <typeinfo>

namespace xournal::pdf::detail
{
    namespace
    {
        OpenedPdf Open(std::string const& description, std::function<void(QPDF&)> const& process)
        {
            auto load = [&] {
                auto pdf = std::make_shared<QPDF>();
                pdf->setSuppressWarnings(true); // collected with getWarnings() and logged
                process(*pdf);
                return pdf;
            };
            OpenedPdf opened;
            opened.pdf = load();
            try
            {
                QPDFPageDocumentHelper(*opened.pdf).flattenAnnotations(0, an_hidden | an_no_view);
            }
            catch (std::exception const&)
            {
                // As the C# export: without its annotations rather than not at all - logged and reported.
                LogException(std::format("pdf: annotations of {} not flattened; used without them", description));
                LogWarnings(*opened.pdf, description);
                opened.annotationsLeftOut = true;
                opened.pdf = load(); // flattening may have changed the document halfway
            }
            LogWarnings(*opened.pdf, description);
            opened.pages = QPDFPageDocumentHelper(*opened.pdf).getAllPages();
            if (opened.pages.empty())
            {
                ThrowInvalidData(description + " has no pages");
            }
            return opened;
        }
    }

    std::string QpdfPath(std::filesystem::path const& file)
    {
        return ToUtf8(Paths::Full(file).wstring());
    }

    void LogWarnings(QPDF& pdf, std::string const& description)
    {
        for (auto const& warning : pdf.getWarnings())
        {
            Log(std::format("pdf: qpdf warning ({}): {}", description, warning.what()));
        }
    }

    OpenedPdf OpenFlattened(std::filesystem::path const& file)
    {
        std::string utf8 = QpdfPath(file);
        return Open(utf8, [&](QPDF& pdf) { pdf.processFile(utf8.c_str()); });
    }

    OpenedPdf OpenFlattened(std::span<std::uint8_t const> data, std::string const& description)
    {
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(data.begin(), data.end());
        OpenedPdf opened = Open(description, [&](QPDF& pdf) {
            pdf.processMemoryFile(description.c_str(), reinterpret_cast<char const*>(bytes->data()), bytes->size());
        });
        opened.data = bytes;
        return opened;
    }

    QPDFObjectHandle ForeignPages::FormOf(Source& source, int pageIndex)
    {
        int index = std::clamp(pageIndex, 0, static_cast<int>(source.opened.pages.size()) - 1);
        auto found = source.forms.find(index);
        if (found != source.forms.end())
        {
            return found->second;
        }
        // Content and resources of the page as a form XObject (/BBox = the page's TrimBox, /Matrix = /Rotate and
        // /UserUnit), copied into the output with everything it refers to.
        auto form = out.copyForeignObject(source.opened.pages[static_cast<size_t>(index)].getFormXObjectForPage());
        source.forms.emplace(index, form);
        return form;
    }

    std::optional<QPDFObjectHandle> ForeignPages::PageForm(std::filesystem::path const& file, int pageIndex)
    {
        std::wstring key = Paths::Full(file).wstring();
        std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
        auto it = files.find(key);
        if (it == files.end())
        {
            if (!Paths::FileExists(file))
            {
                return std::nullopt;
            }
            Source source{ OpenFlattened(file), {} };
            annotationsLeftOut = annotationsLeftOut || source.opened.annotationsLeftOut;
            it = files.emplace(key, std::move(source)).first;
        }
        return FormOf(it->second, pageIndex);
    }

    QPDFObjectHandle ForeignPages::FirstPageOf(std::span<std::uint8_t const> data)
    {
        auto source = std::make_unique<Source>(Source{ OpenFlattened(data, "PDF picture"), {} });
        annotationsLeftOut = annotationsLeftOut || source->opened.annotationsLeftOut;
        memory.push_back(std::move(source));
        return FormOf(*memory.back(), 0);
    }

    QPDFObjectHandle::Rectangle ForeignPages::TransformedBox(QPDFObjectHandle form)
    {
        auto dict = form.getDict();
        auto bbox = dict.getKey("/BBox");
        if (!bbox.isRectangle())
        {
            ThrowInvalidData("pdf: form of a PDF page without a valid box");
        }
        QPDFMatrix matrix;
        if (dict.getKey("/Matrix").isMatrix())
        {
            matrix = QPDFMatrix(dict.getKey("/Matrix").getArrayAsMatrix());
        }
        return matrix.transformRectangle(bbox.getArrayAsRectangle());
    }
}
