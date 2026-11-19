#include "xournal/model/CopyPages.h"

namespace xournal
{
    std::vector<std::shared_ptr<XoppPage>> CopyPages(XoppDocument& source, std::vector<int> const& indices,
        std::function<int(int)> const& pdfPageOf)
    {
        std::vector<std::shared_ptr<XoppPage>> copies;
        for (int index : indices)
        {
            auto const& from = source.Pages().at(static_cast<size_t>(index));
            auto element = from->Snapshot();
            auto background = element->Element(L"background");
            if (from->IsPdf() && background)
            {
                background->RemoveAttribute(L"domain");
                background->RemoveAttribute(L"filename");
                background->SetAttribute(L"pageno", std::to_wstring(pdfPageOf(from->PdfPageIndex()) + 1));
            }
            if (from->IsImageBackground() && !from->BackgroundImagePath() && background &&
                background->AttributeOr(L"domain", L"") == L"clone")
            {
                // A clone of a page that could not be resolved would clone a page of the target: the copy gets the white
                // page the source shows for it, written as upstream writes a plain background.
                // The background's name stays (upstream writes it first).
                auto plain = XmlElement::Make(L"background");
                if (auto name = background->Attribute(L"name"))
                {
                    plain->SetAttribute(L"name", *name);
                }
                for (auto const& attribute : Background::Solid(L"plain")->Attributes())
                {
                    plain->AddAttribute(attribute);
                }
                element->InsertBefore(background.get(), std::move(plain));
                background->Remove();
            }
            auto page = XoppPage::FromXml(element);
            page->SetBackgroundImagePath(from->IsImageBackground() ? from->BackgroundImagePath() : std::nullopt);
            copies.push_back(std::move(page));
        }
        return copies;
    }
}
