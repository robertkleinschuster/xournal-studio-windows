#include "References.h"

#include "xournal/base/Paths.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace xournal::References
{
    namespace
    {
        // A path reference as upstream's SaveHandler writes one: relative to the .xopp's folder (upstream's default
        // storage mode, keeps git-synced folders portable) unless the loaded file used a rooted one; '/' separators.
        std::wstring AssetReference(path const& file, path const& target, bool absolute)
        {
            path full = Paths::Full(file);
            return Paths::WithForwardSlashes(absolute ? full : Paths::Relative(Paths::DirectoryOf(target), full));
        }

        // An "attach" reference is kept where it names exactly the document's own attachment, or the copy made of it for
        // this write - never a file that merely has the name.
        bool IsOwnAttachment(path const& attachedFile, std::optional<path> const& source, AttachmentCopies const& copies)
        {
            if (!source)
            {
                return false;
            }
            if (Paths::Same(attachedFile, *source))
            {
                return true;
            }
            auto copy = CopyOf(copies, *source);
            return copy && Paths::Same(attachedFile, *copy);
        }

        bool KeepsLoadedPdfReference(XoppDocument const& doc, path const& target, AttachmentCopies const& copies)
        {
            auto const& [domain, filename] = doc.LoadedPdfReference();
            if (!filename)
            {
                return false;
            }
            if (!doc.PdfPath())
            {
                return true;
            }
            if (domain == L"attach")
            {
                return IsOwnAttachment(target.native() + L"." + *filename, doc.PdfPath(), copies);
            }
            return Paths::Same(Paths::DirectoryOf(target) / *filename, *doc.PdfPath());
        }
    }

    std::optional<path> CopyOf(AttachmentCopies const& copies, path const& source)
    {
        auto it = std::find_if(copies.begin(), copies.end(), [&](AttachmentCopy const& c) { return Paths::Same(c.source, source); });
        return it == copies.end() ? std::nullopt : std::optional<path>(it->copy);
    }

    void WritePdfReference(XoppDocument const& doc, XmlElement& background, bool firstPdf, path const& target,
        AttachmentCopies const& copies)
    {
        // Rewritten in upstream's order (name, type, domain, filename, pageno); the background's name and attributes of
        // other programs are kept, only the reference is the app's.
        std::vector<XmlAttribute> loadedAttributes = background.Attributes();
        auto loadedValue = [&](std::wstring_view name) -> std::optional<std::wstring>
        {
            auto it = std::find_if(loadedAttributes.begin(), loadedAttributes.end(), [&](XmlAttribute const& a) { return a.name == name; });
            return it == loadedAttributes.end() ? std::nullopt : std::optional<std::wstring>(it->value);
        };
        std::optional<std::wstring> pageNo = loadedValue(L"pageno");
        std::optional<std::wstring> name = loadedValue(L"name");
        background.ClearAttributes();
        if (name)
        {
            background.SetAttribute(L"name", *name);
        }
        background.SetAttribute(L"type", L"pdf");
        auto const& loaded = doc.LoadedPdfReference();
        if (firstPdf && KeepsLoadedPdfReference(doc, target, copies))
        {
            background.SetAttribute(L"domain", loaded.domain.value_or(L"absolute"));
            background.SetAttribute(L"filename", *loaded.filename);
        }
        else if (firstPdf && doc.PdfPath())
        {
            bool rooted = loaded.filename && Paths::IsRooted(*loaded.filename);
            background.SetAttribute(L"domain", L"absolute");
            background.SetAttribute(L"filename", AssetReference(*doc.PdfPath(), target, rooted));
        }
        if (pageNo)
        {
            background.SetAttribute(L"pageno", *pageNo);
        }
        for (auto const& attribute : loadedAttributes)
        {
            bool reference = attribute.name == L"name" || attribute.name == L"type" || attribute.name == L"domain" ||
                attribute.name == L"filename" || attribute.name == L"pageno";
            if (!reference)
            {
                background.AddAttribute(attribute);
            }
        }
    }

    void WritePictureReference(XoppDocument const& doc, XoppPage const& page, XmlElement& background, path const& target,
        int pageIndex, AttachmentCopies const& copies)
    {
        auto const& picture = page.BackgroundImagePath();
        if (!picture)
        {
            return; // names nothing the app can resolve: kept as it was
        }
        std::wstring domain = background.AttributeOr(L"domain", L"");
        std::wstring const* filename = background.Attribute(L"filename");
        if (auto source = page.BackgroundCloneSource())
        {
            auto const& pages = doc.Pages();
            auto it = std::find(pages.begin(), pages.end(), source);
            int index = it == pages.end() ? -1 : static_cast<int>(it - pages.begin());
            if (index >= 0 && index < pageIndex)
            {
                background.SetAttribute(L"filename", std::to_wstring(index));
                return;
            }
            // its page was removed or is no longer before it: the clone names the picture itself
        }
        else if (filename)
        {
            path named = PictureFile(target, domain, *filename);
            bool keep = domain == L"attach" ? IsOwnAttachment(named, picture, copies) : Paths::Same(named, *picture);
            if (keep)
            {
                return;
            }
        }
        bool rooted = filename && Paths::IsRooted(*filename);
        background.SetAttribute(L"domain", L"absolute");
        background.SetAttribute(L"filename", AssetReference(*picture, target, rooted));
    }
}
