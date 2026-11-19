#include "References.h"

#include "xournal/base/Paths.h"
#include "xournal/model/XoppFormat.h"

#include <vector>

namespace xournal::References
{
    path PictureFile(path const& target, std::wstring const& domain, std::wstring const& filename)
    {
        if (domain == L"attach")
        {
            return Paths::Full(target.native() + L"." + filename);
        }
        return Paths::Full(Paths::DirectoryOf(target) / filename);
    }

    std::optional<path> ResolvePdf(path const& referenceBase, PdfReference const& reference)
    {
        if (!reference.filename || reference.filename->empty())
        {
            return std::nullopt;
        }
        std::wstring const& filename = *reference.filename;
        std::vector<path> candidates;
        if (reference.domain == L"attach")
        {
            path withoutExtension = referenceBase;
            withoutExtension.replace_extension();
            candidates.push_back(withoutExtension.native() + L".xopp." + filename);
            candidates.push_back(referenceBase.native() + L"." + filename);
        }
        else
        {
            path dir = Paths::DirectoryOf(referenceBase);
            candidates.push_back(Paths::Full(dir / filename));
            // Moved documents: Xournal also looks for the PDF next to the .xopp.
            candidates.push_back(dir / path(Paths::Full(dir / filename)).filename());
        }
        for (auto const& candidate : candidates)
        {
            if (Paths::FileExists(candidate))
            {
                return candidate;
            }
        }
        return candidates.front(); // missing: still named, so saving keeps the reference
    }

    void ResolvePictureBackgrounds(XoppDocument::PageList const& pages, path const& referenceBase)
    {
        for (auto const& page : pages)
        {
            if (!page->IsImageBackground())
            {
                continue;
            }
            XmlElement const& background = page->Background();
            std::wstring domain = background.AttributeOr(L"domain", L"");
            std::wstring filename = background.AttributeOr(L"filename", L"");
            if (filename.empty())
            {
                continue;
            }
            if (domain == L"clone")
            {
                auto index = XoppFormat::ParseInt(filename);
                if (index && *index >= 0 && *index < static_cast<int>(pages.size()))
                {
                    page->SetBackgroundImagePath(pages[*index]->BackgroundImagePath());
                    page->SetBackgroundCloneSource(pages[*index]);
                }
                continue;
            }
            // Also when missing (white, as upstream): saving keeps naming it.
            page->SetBackgroundImagePath(PictureFile(referenceBase, domain, filename));
        }
    }
}
