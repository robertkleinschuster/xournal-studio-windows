#include "xournal/model/Attachments.h"

namespace xournal
{
    std::vector<Attachment> FindAttachments(XoppDocument const& doc)
    {
        std::vector<Attachment> attachments;
        auto const& pdf = doc.LoadedPdfReference();
        if (pdf.domain == L"attach" && pdf.filename)
        {
            attachments.push_back({ doc.PdfPath(), *pdf.filename });
        }
        for (auto const& page : doc.Pages())
        {
            if (!page->IsImageBackground() || page->BackgroundCloneSource())
            {
                continue;
            }
            XmlElement const& background = page->Background();
            auto filename = background.Attribute(L"filename");
            if (background.AttributeOr(L"domain", L"") == L"attach" && filename)
            {
                attachments.push_back({ page->BackgroundImagePath(), *filename });
            }
        }
        return attachments;
    }
}
