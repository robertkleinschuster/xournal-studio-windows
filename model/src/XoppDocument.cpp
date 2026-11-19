#include "xournal/model/XoppDocument.h"

#include "References.h"
#include "xournal/base/Error.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/model/XoppFormat.h"

#include <windows.h>

#include <algorithm>
#include <set>

namespace xournal
{
    namespace
    {
        XmlElementPtr NewRoot()
        {
            auto root = XmlElement::Make(L"xournal", { { L"creator", std::wstring(XoppFormat::Creator) },
                { L"fileversion", std::wstring(XoppFormat::FileVersion) } });
            auto title = XmlElement::Make(L"title");
            title->SetValue(std::wstring(XoppFormat::Title));
            root->Append(title);
            return root;
        }

        std::filesystem::path TempFolder()
        {
            wchar_t buffer[MAX_PATH + 1];
            DWORD length = GetTempPathW(MAX_PATH + 1, buffer);
            if (length == 0)
            {
                ThrowLastError("GetTempPath");
            }
            return std::wstring(buffer, length);
        }

        // A MrWriter document (<MrWriter version>, read by upstream with the same structure) becomes a Xournal++ one, as
        // upstream saves it: the root is named <xournal>; its attributes and everything in it stay.
        XmlElementPtr AsXournal(XmlElement& mrWriter)
        {
            auto root = std::make_shared<XmlElement>(L"xournal");
            for (auto const& attribute : mrWriter.Attributes())
            {
                root->AddAttribute(attribute);
            }
            std::vector<XmlNode> nodes = mrWriter.Nodes();
            for (auto& node : nodes)
            {
                if (node.element)
                {
                    root->Append(node.element->Remove());
                }
                else
                {
                    root->AppendNode(std::move(node));
                }
            }
            return root;
        }

        std::optional<std::wstring> AttributeValue(XmlElement const& e, std::wstring_view name)
        {
            auto value = e.Attribute(name);
            return value ? std::optional<std::wstring>(*value) : std::nullopt;
        }
    }

    XoppDocument::XoppDocument(XmlElementPtr root) : root(std::move(root)) {}

    std::unique_ptr<XoppDocument> XoppDocument::CreateNew()
    {
        auto doc = std::make_unique<XoppDocument>(NewRoot());
        doc->createdAt = DateTime::Now();
        doc->pages.push_back(XoppPage::CreateGraph(XoppFormat::A4Width, XoppFormat::A4Height));
        return doc;
    }

    std::unique_ptr<XoppDocument> XoppDocument::FromPdf(path pdf, std::vector<Size> const& pageSizes)
    {
        auto doc = std::make_unique<XoppDocument>(NewRoot());
        doc->pdfPath = std::move(pdf);
        doc->createdAt = DateTime::Now();
        for (size_t i = 0; i < pageSizes.size(); ++i)
        {
            doc->pages.push_back(XoppPage::CreatePdf(pageSizes[i].width, pageSizes[i].height, static_cast<int>(i)));
        }
        return doc;
    }

    std::unique_ptr<XoppDocument> XoppDocument::FromXml(XmlElementPtr root, path const& referenceBase)
    {
        if (root && root->Name() == L"MrWriter")
        {
            root = AsXournal(*root);
        }
        if (!root || root->Name() != L"xournal")
        {
            ThrowInvalidData("not a Xournal++ document (no <xournal> element)");
        }
        auto doc = std::make_unique<XoppDocument>(root);
        for (auto const& pageElement : root->Elements(L"page"))
        {
            doc->pages.push_back(XoppPage::FromXml(pageElement->Remove()));
        }
        for (auto const& page : doc->pages)
        {
            if (page->IsPdf())
            {
                XmlElement const& background = page->Background();
                doc->loadedPdfReference = { AttributeValue(background, L"domain"), AttributeValue(background, L"filename") };
                doc->pdfPath = References::ResolvePdf(referenceBase, doc->loadedPdfReference);
                break;
            }
        }
        References::ResolvePictureBackgrounds(doc->pages, referenceBase);
        return doc;
    }

    std::vector<std::filesystem::path> XoppDocument::Files() const
    {
        std::vector<path> candidates;
        if (filePath) candidates.push_back(*filePath);
        if (pdfPath) candidates.push_back(*pdfPath);
        for (auto const& page : pages)
        {
            if (page->BackgroundImagePath())
            {
                candidates.push_back(*page->BackgroundImagePath());
            }
        }
        std::vector<path> files;
        for (auto const& candidate : candidates)
        {
            bool known = std::any_of(files.begin(), files.end(),
                [&](path const& f) { return EqualsIgnoreCase(f.native(), candidate.native()); });
            if (!known)
            {
                files.push_back(candidate);
            }
        }
        return files;
    }

    bool XoppDocument::Uses(path const& item) const
    {
        std::wstring folder = Paths::Full(item).native();
        while (folder.size() > 3 && (folder.back() == L'\\' || folder.back() == L'/'))
        {
            folder.pop_back();
        }
        for (auto const& file : Files())
        {
            std::wstring full = Paths::Full(file).native();
            if (EqualsIgnoreCase(full, folder) || StartsWithIgnoreCase(full, folder + L"\\"))
            {
                return true;
            }
        }
        return false;
    }

    std::wstring XoppDocument::SuggestedName() const
    {
        return DocumentNames::NameFor(filePath, pdfPath, createdAt.value_or(DateTime::Now()));
    }

    std::filesystem::path XoppDocument::ReferenceBase() const
    {
        if (filePath)
        {
            return *filePath;
        }
        path folder = pdfPath ? Paths::DirectoryOf(*pdfPath) : TempFolder();
        return folder / (SuggestedName() + L".xopp");
    }

    std::vector<int> XoppDocument::PdfPagesOf(std::vector<int> const& indices) const
    {
        std::set<int> pdfPages;
        for (int index : indices)
        {
            auto const& page = pages.at(static_cast<size_t>(index));
            if (page->IsPdf())
            {
                pdfPages.insert(page->PdfPageIndex());
            }
        }
        return { pdfPages.begin(), pdfPages.end() };
    }

    void XoppDocument::AdoptAttachmentCopies(AttachmentCopies const& copies)
    {
        for (auto const& [source, copy] : copies)
        {
            if (pdfPath && Paths::Same(*pdfPath, source))
            {
                pdfPath = copy;
            }
            for (auto const& page : pages)
            {
                if (page->BackgroundImagePath() && Paths::Same(*page->BackgroundImagePath(), source))
                {
                    page->SetBackgroundImagePath(copy);
                }
            }
        }
    }
}
