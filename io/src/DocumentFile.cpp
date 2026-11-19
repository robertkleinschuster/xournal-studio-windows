#include "xournal/io/DocumentFile.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/io/Files.h"
#include "xournal/io/Gzip.h"
#include "xournal/io/XmlText.h"
#include "xournal/model/Attachments.h"
#include "xournal/model/Snapshot.h"

#include <algorithm>
#include <format>

namespace xournal::io::DocumentFile
{
    namespace
    {
        // Copies the attachments next to `target` as "<target>.<filename>". A missing source, a file already there or
        // one already copied is skipped (the reference is then written as a path); each copy goes into `copies` at once,
        // so a failure takes back exactly those.
        void CopyAttachments(std::vector<Attachment> const& attachments, path const& target, AttachmentCopies& copies)
        {
            for (auto const& [source, filename] : attachments)
            {
                if (!source || !Paths::FileExists(*source))
                {
                    continue;
                }
                path from = Paths::Full(*source), to = Paths::Full(target.native() + L"." + filename);
                bool copied = std::any_of(copies.begin(), copies.end(), [&](auto const& c) { return Paths::Same(c.source, from); });
                if (Paths::Same(from, to) || copied || Paths::FileExists(to))
                {
                    continue; // never over another file of that name
                }
                Files::CopyNew(from, to);
                copies.push_back({ from, to });
            }
        }

        // Takes back the copies of a failed write; failures doing so go along with the cause.
        [[noreturn]] void TakeBack(AttachmentCopies const& copies, Error const& cause)
        {
            std::string leftBehind;
            for (auto const& copy : copies)
            {
                try
                {
                    Files::Delete(copy.copy);
                }
                catch (Error const& e)
                {
                    leftBehind += std::format("; {}", e.what());
                }
            }
            if (leftBehind.empty())
            {
                throw cause;
            }
            std::string message = std::format("{}; copies of the attachments were left behind{}", cause.what(), leftBehind);
            LogError(message);
            throw Error(message, cause.Code());
        }

        AttachmentCopies WriteWithAttachments(XoppDocument& doc, path const& file)
        {
            AttachmentCopies copies;
            try
            {
                CopyAttachments(FindAttachments(doc), file, copies);
                auto snapshot = TakeSnapshot(doc, file, copies);
                Write(*snapshot, file);
                // The PDF reference now is what this file says (attach where copied, a path where not).
                for (auto const& page : snapshot->Elements(L"page"))
                {
                    auto background = page->Element(L"background");
                    if (background && Background::IsPdf(*background) && background->Attribute(L"filename"))
                    {
                        auto domain = background->Attribute(L"domain");
                        doc.SetLoadedPdfReference({ domain ? std::optional<std::wstring>(*domain) : std::nullopt,
                            *background->Attribute(L"filename") });
                        break;
                    }
                }
            }
            catch (Error const& e)
            {
                TakeBack(copies, e);
            }
            return copies;
        }
    }

    XmlElementPtr ReadXml(path const& file)
    {
        auto bytes = Files::ReadAll(file);
        if (Gzip::IsGzip(bytes))
        {
            bytes = Gzip::Decompress(bytes);
        }
        return ParseXml(bytes);
    }

    std::unique_ptr<XoppDocument> Load(path const& file)
    {
        auto doc = XoppDocument::FromXml(ReadXml(file), file);
        doc->SetFilePath(file);
        return doc;
    }

    std::unique_ptr<XoppDocument> LoadCopy(path const& copy, std::optional<path> const& documentPath, path const& referenceBase)
    {
        auto doc = XoppDocument::FromXml(ReadXml(copy), referenceBase);
        doc->SetFilePath(documentPath);
        return doc;
    }

    void Write(XmlElement const& snapshot, path const& file)
    {
        Files::WriteReplacing(file, Gzip::Compress(SerializeXml(snapshot)));
    }

    void Save(XoppDocument& doc, path const& file)
    {
        auto snapshot = TakeSnapshot(doc, file);
        Write(*snapshot, file);
        doc.SetFilePath(file);
    }

    void SaveAs(XoppDocument& doc, path const& file)
    {
        auto copies = WriteWithAttachments(doc, file);
        doc.SetFilePath(file);
        doc.AdoptAttachmentCopies(copies);
    }

    void SaveCopy(XoppDocument& doc, path const& file)
    {
        WriteWithAttachments(doc, file);
    }
}
