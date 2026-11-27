#include "PdfExportCommand.h"

#include "Dialogs.h"
#include "ErrorText.h"
#include "UiQueue.h"

#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/import/IpadExport.h"
#include "xournal/pdf/DocumentExport.h"

#include <format>

namespace xournal::app
{
    namespace
    {
        // The status message for a finished export.
        std::wstring Describe(std::filesystem::path const& target, pdf::ExportResult const& result,
            std::optional<std::filesystem::path> const& background, bool& complete, import::IpadExportResult const* ipad = nullptr)
        {
            std::wstring pages = result.pages == 1 ? L"1 Seite" : std::format(L"{} Seiten", result.pages);
            std::wstring text = std::format(L"{} ({}).", target.filename().native(), pages);
            if (result.missingBackgrounds > 0)
            {
                text += std::format(L" {} Seite(n) ohne ihren Hintergrund (nicht gefunden oder nicht lesbar", result.missingBackgrounds);
                text += background && !std::filesystem::exists(*background) ? std::format(L"; PDF: {}).", background->native()) : L").";
            }
            if (result.skippedElements > 0)
            {
                text += std::format(L" {} Element(e), die diese App noch nicht darstellt, fehlen darin.", result.skippedElements);
            }
            if (result.annotationsLeftOut)
            {
                text += L" Anmerkungen der Hintergrund-PDF (z. B. Unterschriften aus anderen Apps) fehlen darin: die PDF lässt "
                        L"sich nicht verändern.";
            }
            complete = result.missingBackgrounds + result.skippedElements == 0 && !result.annotationsLeftOut;
            if (ipad)
            {
                text += std::format(L" {} Strich(e) auf {} Seite(n) sind auf dem iPad bearbeitbar", ipad->strokes, ipad->drawings);
                text += ipad->flatStrokes > 0 ? std::format(L"; {} weitere (Textmarker, gestrichelt, gefüllt) sind fest in der Seite.", ipad->flatStrokes) : L".";
                return std::format(L"{}: {}", complete ? L"Für iPad exportiert" : L"Für iPad exportiert – unvollständig", text);
            }
            return std::format(L"{}: {}", complete ? L"Als PDF exportiert" : L"Als PDF exportiert – unvollständig", text);
        }
    }

    PdfExportCommand::PdfExportCommand(HWND owner, DocumentView& view, std::function<void(std::wstring const&, MessageKind)> message)
        : owner(owner), view(view), message(std::move(message))
    {
    }

    void PdfExportCommand::Export(std::shared_ptr<XoppDocument> const& doc, Kind kind)
    {
        if (!doc) return;
        if (running)
        {
            message(L"Ein Export läuft noch.", MessageKind::Info);
            return;
        }
        auto background = doc->PdfPath();
        auto base = doc->FilePath() ? doc->FilePath() : doc->PdfPath();
        std::filesystem::path folder = base ? base->parent_path() : std::filesystem::path();
        std::wstring name = doc->SuggestedName();
        if (kind == Kind::Ipad) name += L" (iPad)";
        if (background && !folder.empty() && Paths::Same(folder / (name + L".pdf"), *background))
        {
            name += L" (mit Notizen)"; // the lecture PDF itself stays untouched
        }
        auto target = Dialogs::SavePdf(owner, folder, name);
        if (!target) return;
        if (background && Paths::Same(*target, *background))
        {
            message(L"Nicht exportiert: das ist die PDF, auf der das Dokument liegt; bitte einen anderen Namen wählen.",
                MessageKind::Warning);
            return;
        }

        // The document as the ink shows it now, taken where its ink may be read; the PDF is written elsewhere.
        pdf::DocumentExportSource source;
        try
        {
            view.CaptureInk([&] { source = pdf::PrepareExport(*doc); });
        }
        catch (...)
        {
            LogException(std::format("export PDF to {}: reading the document", ToUtf8(target->native())));
            message(std::format(L"Export fehlgeschlagen: {}", ErrorText()), MessageKind::Error);
            return;
        }
        running = true;
        UiQueue::RunInBackground("export PDF", [this, source = std::move(source), file = *target, background, kind]
        {
            std::optional<pdf::ExportResult> result;
            std::optional<import::IpadExportResult> ipad;
            std::wstring error;
            try
            {
                if (kind == Kind::Ipad)
                {
                    ipad = import::ExportForIpad(source, file);
                    result = ipad->pdf;
                }
                else
                {
                    result = pdf::ExportDocument(source, file);
                }
            }
            catch (...)
            {
                LogException(std::format("export PDF to {}", ToUtf8(file.native())));
                error = ErrorText();
            }
            UiQueue::Post("export PDF done", [this, result, ipad, error, file, background]
            {
                running = false;
                if (!result)
                {
                    message(std::format(L"Export fehlgeschlagen ({}): {}", file.filename().native(), error), MessageKind::Error);
                    return;
                }
                bool complete = true;
                std::wstring text = Describe(file, *result, background, complete, ipad ? &*ipad : nullptr);
                message(text, complete ? MessageKind::Success : MessageKind::Warning);
            });
        });
    }
}
