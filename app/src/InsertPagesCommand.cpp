#include "InsertPagesCommand.h"

#include "Bitmaps.h"
#include "Dialogs.h"
#include "ErrorText.h"
#include "InsertPagesDialog.h"
#include "UiQueue.h"

#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/io/DocumentFile.h"
#include "xournal/io/Files.h"
#include "xournal/model/CopyPages.h"
#include "xournal/pdf/DocumentExport.h"
#include "xournal/pdf/PdfPageAppender.h"

#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr UINT ThumbnailPixels = 120;

        // Render keys of the dialog's thumbnails: their own generation range (one per dialog).
        std::atomic<std::uint64_t> dialogs{ 0xF0000000 };

        bool IsDocument(std::filesystem::path const& file) { return EqualsIgnoreCase(file.extension().native(), L".xopp"); }

        // `content` (premultiplied) over `background`, both the same size.
        void Over(CpuBitmap& background, CpuBitmap const& content)
        {
            if (background.width != content.width || background.height != content.height) return;
            for (size_t i = 0; i + 3 < background.pixels.size(); i += 4)
            {
                unsigned alpha = content.pixels[i + 3];
                for (size_t c = 0; c < 4; ++c)
                {
                    background.pixels[i + c] = static_cast<std::uint8_t>(content.pixels[i + c] + background.pixels[i + c] * (255 - alpha) / 255);
                }
            }
        }

        // What lies on a page, drawn by the PDF export (this one page, without its PDF) and rendered.
        std::shared_ptr<CpuBitmap> RenderContent(PdfPageRenderer& renderer, XmlElementPtr snapshot,
            std::optional<std::filesystem::path> picture, UINT width, UINT height, bool transparent)
        {
            auto root = XmlElement::Make(L"xournal");
            root->Append(std::move(snapshot));
            pdf::DocumentExportSource source{ root, std::nullopt, { std::move(picture) } };
            pdf::ExportResult result;
            auto pages = pdf::PagesOf(source, result);
            auto bytes = pdf::ExportToMemory(pages, result);
            auto document = Bitmaps::LoadPdf(bytes);
            return renderer.Render(document.GetPage(0), width, height, transparent);
        }
    }

    struct InsertPagesCommand::Source
    {
        std::filesystem::path file;
        std::shared_ptr<XoppDocument> target;                 // the document the pages go into
        std::unique_ptr<XoppDocument> document;               // a .xopp source
        std::optional<std::filesystem::path> pdfPath;         // the PDF the source's PDF pages come from (if there)
        winrt::Windows::Data::Pdf::PdfDocument pdf{ nullptr }; // loaded from memory (agile)
        std::vector<Size> pdfSizes;                           // its pages in points
        std::uint64_t keys = 0;                               // render keys of this dialog

        int Count() const { return document ? static_cast<int>(document->Pages().size()) : static_cast<int>(pdfSizes.size()); }
    };

    InsertPagesCommand::InsertPagesCommand(HWND owner, DocumentSession& session, DocumentView& view,
        std::function<void(std::wstring const&, ToastKind)> message)
        : owner(owner), session(session), view(view), message(std::move(message))
    {
    }

    void InsertPagesCommand::Insert(int around)
    {
        auto target = session.Document();
        if (!target || running) return;
        std::optional<std::filesystem::path> folder;
        if (auto file = session.FilePath()) folder = file->parent_path();
        auto file = Dialogs::OpenPagesSource(owner, folder);
        if (!file) return;
        running = true;
        auto source = std::make_shared<Source>();
        source->file = *file;
        source->target = target;
        source->keys = dialogs++;
        UiQueue::RunInBackground("insert pages: load", [this, source, around]
        {
            std::wstring error;
            try
            {
                if (IsDocument(source->file))
                {
                    source->document = io::DocumentFile::Load(source->file);
                    auto pdf = source->document->PdfPath();
                    if (pdf && Paths::FileExists(*pdf)) source->pdfPath = pdf;
                }
                else
                {
                    source->pdfPath = source->file;
                }
                if (source->pdfPath)
                {
                    // In memory: the file stays free (and a document's PDF may be appended to in a moment).
                    auto bytes = io::Files::ReadAll(*source->pdfPath);
                    source->pdf = Bitmaps::LoadPdf(bytes);
                    for (UINT i = 0; i < source->pdf.PageCount(); ++i)
                    {
                        auto size = source->pdf.GetPage(i).Size(); // DIPs
                        source->pdfSizes.push_back({ size.Width * 72.0 / 96.0, size.Height * 72.0 / 96.0 });
                    }
                }
            }
            catch (...)
            {
                LogException(std::format("insert pages: loading {}", ToUtf8(source->file.native())));
                error = ErrorText();
            }
            UiQueue::Post("insert pages: ask", [this, source, around, error]
            {
                if (!error.empty())
                {
                    running = false;
                    message(std::format(L"Seiten konnten nicht eingefügt werden ({}): {}", source->file.filename().native(), error),
                        ToastKind::Error);
                    return;
                }
                Ask(source, around);
            });
        });
    }

    void InsertPagesCommand::Ask(std::shared_ptr<Source> source, int around)
    {
        auto finish = [this] { running = false; };
        if (source->Count() == 0)
        {
            finish();
            message(std::format(L"Keine Seiten: {} hat keine Seiten.", source->file.filename().native()), ToastKind::Warning);
            return;
        }
        // What happens to the document's PDF (the C# wording).
        auto const& target = *source->target;
        std::wstring copied = source->document ? L"Die Seiten werden mit allem, was darauf liegt, kopiert. " : L"";
        std::wstring where;
        bool needsPdf = !source->document || std::any_of(source->document->Pages().begin(), source->document->Pages().end(),
            [](auto const& p) { return p->IsPdf(); });
        if (!needsPdf) where = copied;
        else if (!source->pdfPath)
            where = copied + std::format(L"Das PDF der Seiten mit PDF-Hintergrund fehlt ({}): diese Seiten lassen sich nicht einfügen.",
                source->document && source->document->PdfPath() ? source->document->PdfPath()->native() : std::wstring(L"nicht genannt"));
        else if (target.PdfPath() && Paths::Same(*target.PdfPath(), *source->pdfPath))
            where = source->document ? copied : L"Es ist das PDF des Dokuments: Die Seiten werden nur eingefügt.";
        else if (!target.PdfPath())
            where = copied + std::format(L"Das Dokument verwendet dann „{}“ als PDF-Hintergrund{}", source->pdfPath->filename().native(),
                source->document ? L" (sobald eine gewählte Seite eine PDF-Seite zeigt)." : L".");
        else
            where = copied + std::format(L"{} ans Ende von „{}“ angehängt – das PDF des Dokuments wird dabei sofort geändert "
                L"(wie in Xournal++ hat ein Dokument ein PDF), seine bisherigen Seiten bleiben.",
                source->document ? L"PDF-Seiten der gewählten Seiten werden" : L"Die Seiten werden", target.PdfPath()->filename().native());

        InsertPagesRequest request;
        request.file = source->file.filename().native();
        request.count = source->Count();
        request.around = around;
        request.where = where;
        request.aspect = [source](int i)
        {
            if (source->document)
            {
                auto const& page = source->document->Pages()[i];
                return page->Height() / std::max(1.0, page->Width());
            }
            return source->pdfSizes[i].height / std::max(1.0, source->pdfSizes[i].width);
        };
        request.render = [this, source](int i, std::function<void(std::shared_ptr<CpuBitmap>)> done)
        {
            // On the render thread: the page's PDF page, and for a document's page what lies on it drawn over it.
            XmlElementPtr snapshot;
            std::optional<std::filesystem::path> picture;
            bool pdfPage = false;
            int pdfIndex = i;
            double aspect;
            if (source->document)
            {
                auto const& page = source->document->Pages()[i];
                snapshot = page->Snapshot();
                picture = page->BackgroundImagePath();
                pdfPage = page->IsPdf();
                pdfIndex = page->PdfPageIndex();
                aspect = page->Height() / std::max(1.0, page->Width());
            }
            else
            {
                pdfPage = true;
                aspect = source->pdfSizes[i].height / std::max(1.0, source->pdfSizes[i].width);
            }
            UINT width = ThumbnailPixels, height = std::max<UINT>(1, static_cast<UINT>(std::lround(ThumbnailPixels * aspect)));
            view.Queue().Submit((source->keys << 32) | static_cast<std::uint32_t>(i), i, std::format("insert dialog thumbnail {}", i + 1),
                [source, snapshot, picture, pdfPage, pdfIndex, width, height, done](PdfPageRenderer& renderer)
            {
                std::shared_ptr<CpuBitmap> bitmap;
                try
                {
                    if (pdfPage && source->pdf && static_cast<UINT>(pdfIndex) < source->pdf.PageCount())
                    {
                        bitmap = renderer.Render(source->pdf.GetPage(static_cast<UINT>(pdfIndex)), width, height, false);
                    }
                    if (snapshot)
                    {
                        auto content = RenderContent(renderer, snapshot, picture, width, height, bitmap != nullptr);
                        if (bitmap) Over(*bitmap, *content);
                        else bitmap = content;
                    }
                }
                catch (...)
                {
                    LogException("insert dialog: a thumbnail (left empty)");
                }
                UiQueue::Post("insert dialog thumbnail", [done, bitmap] { done(bitmap); });
            });
        };

        std::optional<InsertPagesChoice> choice;
        try
        {
            choice = AskInsertPages(owner, request);
        }
        catch (...)
        {
            LogException("insert pages: the dialog");
            message(L"Seiten konnten nicht eingefügt werden: " + ErrorText(), ToastKind::Error);
        }
        std::uint64_t keys = source->keys;
        view.Queue().Drop([keys](std::uint64_t key) { return (key >> 32) == keys; }); // thumbnails not needed any more
        if (!choice || choice->pages.empty())
        {
            finish();
            return;
        }
        Bring(source, choice->pages, choice->before ? around : around + 1);
    }

    void InsertPagesCommand::Bring(std::shared_ptr<Source> source, std::vector<int> chosen, int index)
    {
        auto identity = [](int p) { return p; };
        std::vector<int> needed = source->document ? source->document->PdfPagesOf(chosen) : chosen;
        auto const& target = *source->target;
        if (needed.empty())
        {
            Finish(source, chosen, index, identity, false, false);
            return;
        }
        if (!source->pdfPath)
        {
            running = false;
            message(std::format(L"Seiten nicht eingefügt: das PDF der Seiten fehlt ({}).", source->document && source->document->PdfPath()
                ? source->document->PdfPath()->native() : source->file.filename().native() + L" nennt keines"), ToastKind::Warning);
            return;
        }
        if (target.PdfPath() && Paths::Same(*target.PdfPath(), *source->pdfPath))
        {
            Finish(source, chosen, index, identity, false, false);
            return;
        }
        if (!target.PdfPath())
        {
            source->target->MovePdf(*source->pdfPath); // the document takes the source's PDF as its PDF
            Finish(source, chosen, index, identity, false, true);
            return;
        }
        if (session.refusedWhileSyncing && session.refusedWhileSyncing(L"Seiten an das PDF anhängen"))
        {
            running = false; // appending writes the document's PDF, which git may be working on
            return;
        }
        auto targetPdf = *target.PdfPath();
        UiQueue::RunInBackground("insert pages: append", [this, source, chosen, index, needed, targetPdf]
        {
            int first = -1;
            std::wstring error;
            try
            {
                first = pdf::AppendPages(targetPdf, *source->pdfPath, needed);
            }
            catch (...)
            {
                LogException(std::format("insert pages: appending to {}", ToUtf8(targetPdf.native())));
                error = ErrorText();
            }
            UiQueue::Post("insert pages: appended", [this, source, chosen, index, needed, first, error]
            {
                if (!error.empty())
                {
                    running = false;
                    message(L"Seiten konnten nicht eingefügt werden: " + error, ToastKind::Error);
                    return;
                }
                auto pageOf = [needed, first](int p)
                {
                    return first + static_cast<int>(std::find(needed.begin(), needed.end(), p) - needed.begin());
                };
                Finish(source, chosen, index, pageOf, true, false);
            });
        });
    }

    void InsertPagesCommand::Finish(std::shared_ptr<Source> source, std::vector<int> chosen, int index,
        std::function<int(int)> pageOf, bool appended, bool adopted)
    {
        running = false;
        if (session.Document() != source->target)
        {
            // The insert goes on only into the document it began in (appended PDF pages stay unused at its end).
            message(L"Seiten nicht eingefügt: inzwischen ist ein anderes Dokument geöffnet.", ToastKind::Info);
            return;
        }
        std::vector<std::shared_ptr<XoppPage>> pages;
        if (source->document)
        {
            pages = CopyPages(*source->document, chosen, pageOf);
        }
        else
        {
            for (int p : chosen)
            {
                pages.push_back(XoppPage::CreatePdf(source->pdfSizes[p].width, source->pdfSizes[p].height, pageOf(p)));
            }
        }
        if (appended || adopted)
        {
            view.ReloadPdf(); // the PDF on screen is the document's (new or appended to) - before the pages show
        }
        view.InsertPages(index, std::move(pages));
        std::wstring text = std::format(L"Seiten eingefügt: {} Seite(n) aus {}", chosen.size(), source->file.filename().native());
        text += appended ? std::format(L" – PDF-Seiten an {} angehängt.", source->target->PdfPath()->filename().native()) : L".";
        message(text, ToastKind::Success);
    }
}
