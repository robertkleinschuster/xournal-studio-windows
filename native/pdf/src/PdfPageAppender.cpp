#include "xournal/pdf/PdfPageAppender.h"

#include "QpdfSources.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/io/Files.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <format>
#include <memory>
#include <typeinfo>

namespace xournal::pdf
{
    int AppendPages(std::filesystem::path const& target, std::filesystem::path const& source, std::vector<int> const& pages)
    {
        std::vector<std::uint8_t> bytes;
        int first = 0;
        try
        {
            detail::OpenedPdf from = detail::OpenFlattened(source);
            if (from.annotationsLeftOut)
            {
                Log("pdf append: the source's annotations could not be drawn into its pages; appended without them");
            }
            QPDF to;
            to.setSuppressWarnings(true); // collected and logged
            std::string targetPath = detail::QpdfPath(target);
            to.processFile(targetPath.c_str());
            QPDFPageDocumentHelper toPages(to);
            first = static_cast<int>(toPages.getAllPages().size());
            for (int index : pages)
            {
                if (index < 0 || index >= static_cast<int>(from.pages.size()))
                {
                    ThrowInvalidData(std::format("pdf append: page {} of {} pages", index + 1, from.pages.size()));
                }
                // What the annotations showed is in the content now (flattened); the rest (links, hidden ones) would
                // refer into the source document. The source is only read into memory here, never written.
                auto page = from.pages[static_cast<size_t>(index)];
                page.getObjectHandle().removeKey("/Annots");
                // addPage copies a page of another document (with its resources) into this one.
                toPages.addPage(page, false);
            }
            QPDFWriter writer(to);
            writer.setOutputMemory();
            writer.setDecodeLevel(qpdf_dl_none); // the target's streams stay as they are
            writer.write();
            detail::LogWarnings(*from.pdf, detail::QpdfPath(source));
            detail::LogWarnings(to, targetPath);
            std::unique_ptr<Buffer> buffer(writer.getBuffer());
            bytes.assign(buffer->getBuffer(), buffer->getBuffer() + buffer->getSize());
        }
        catch (Error const&)
        {
            throw;
        }
        catch (std::exception const& e)
        {
            std::string message = std::format("pdf append: {}: {}", typeid(e).name(), e.what());
            LogError(message);
            throw Error(message, E_FAIL);
        }
        io::Files::WriteReplacing(target, bytes); // "<target>~", then moved over the target
        return first;
    }
}
