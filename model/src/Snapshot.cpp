#include "xournal/model/Snapshot.h"

#include "References.h"
#include "xournal/model/XoppFormat.h"
#include "xournal/model/XoppImage.h"
#include "xournal/model/XoppText.h"

namespace xournal
{
    XmlElementPtr TakeSnapshot(XoppDocument& doc, std::filesystem::path const& referenceBase, AttachmentCopies const& copies)
    {
        XmlElement& root = doc.Root();
        root.SetAttribute(L"creator", std::wstring(XoppFormat::Creator));
        if (!root.Attribute(L"fileversion"))
        {
            root.SetAttribute(L"fileversion", std::wstring(XoppFormat::FileVersion));
        }

        auto output = root.Clone();
        bool firstPdf = true;
        int index = 0;
        for (auto const& page : doc.Pages())
        {
            auto element = page->Snapshot();
            if (XoppFormat::ReadNumber(*output, L"fileversion", 4) < 5)
            {
                for (auto const& layer : element->Elements(L"layer"))
                {
                    for (auto const& object : layer->Elements())
                    {
                        if ((XoppImage::IsImage(*object) || XoppText::IsText(*object)) && XoppFormat::ReadMatrix(*object))
                            output->SetAttribute(L"fileversion", L"5");
                    }
                }
            }
            if (auto background = element->Element(L"background"))
            {
                if (Background::IsPdf(*background))
                {
                    References::WritePdfReference(doc, *background, firstPdf, referenceBase, copies);
                    firstPdf = false;
                }
                else if (page->IsImageBackground())
                {
                    References::WritePictureReference(doc, *page, *background, referenceBase, index, copies);
                }
            }
            // A line break before each page: formatting only, so the file stays readable as upstream writes it.
            output->AppendNode({ XmlNodeKind::Whitespace, nullptr, L"\n", {} });
            output->Append(std::move(element));
            ++index;
        }
        output->AppendNode({ XmlNodeKind::Whitespace, nullptr, L"\n", {} });
        return output;
    }
}
