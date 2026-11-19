#include "PdfImportCommand.h"

#include "Dialogs.h"
#include "ErrorText.h"
#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/import/PdfMarkupImport.h"
#include "xournal/io/DocumentFile.h"

#include <objbase.h>

#include <chrono>
#include <format>
#include <set>

namespace xournal::app
{
    namespace
    {
        // Pictures (pasted screenshots: about 1.8 px/pt in the user's PDFs) a little sharper than their source.
        constexpr double PicturePixelsPerPoint = 2;

        std::wstring Summary(import::MarkupImport const& import, std::filesystem::path const& xopp)
        {
            int strokes = 0;
            for (auto const& page : import.elements)
                for (auto const& e : page) strokes += e->Name() == L"stroke";
            std::vector<std::wstring> parts;
            if (strokes > 0) parts.push_back(std::format(L"{} Striche", strokes));
            if (import.texts > 0) parts.push_back(std::format(L"{} Texte", import.texts));
            if (!import.pictures.empty()) parts.push_back(std::format(L"{} Bilder", import.pictures.size()));
            std::wstring list;
            for (size_t i = 0; i < parts.size(); ++i) list += (i ? L", " : L"") + parts[i];
            std::wstring text = std::format(L"PDF importiert: {}: {}.", xopp.filename().native(), parts.empty() ? L"keine Anmerkungen" : list);
            if (import.kept > 0)
            {
                text += std::format(L" {} Anmerkung(en) ohne Darstellung sind in der PDF geblieben.", import.kept);
            }
            return text;
        }

        // A temporary file of this import (and its "~" write file): gone afterwards; a failure is logged, not thrown
        // (the import itself succeeded or already failed with its own error).
        void DeleteTemporary(std::filesystem::path const& file)
        {
            for (auto const& path : { file, std::filesystem::path(file.native() + L"~") })
            {
                if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
                {
                    LogError(std::format("import: temporary file {} not deleted: {}", ToUtf8(path.native()),
                        DescribeHr(HRESULT_FROM_WIN32(GetLastError()))));
                }
            }
        }

        std::wstring NewGuidText()
        {
            GUID guid{};
            Check("CoCreateGuid", CoCreateGuid(&guid));
            wchar_t text[40];
            StringFromGUID2(guid, text, 40);
            return text;
        }
    }

    PdfImportCommand::PdfImportCommand(HWND owner, DocumentSession& session, std::function<void(std::wstring const&, ToastKind)> message)
        : owner(owner), session(session), message(std::move(message))
    {
    }

    void PdfImportCommand::Import()
    {
        if (running)
        {
            message(L"Ein Import läuft noch.", ToastKind::Info);
            return;
        }
        std::optional<std::filesystem::path> folder;
        if (auto file = session.FilePath()) folder = file->parent_path();
        auto source = Dialogs::OpenPdf(owner, folder, L"PDF mit iPad-Anmerkungen importieren");
        if (!source) return;
        auto xopp = Dialogs::SaveDocument(owner, source->parent_path(), source->stem().native());
        if (!xopp || (session.refusedWhileSyncing && session.refusedWhileSyncing(L"Importieren"))) return;

        std::filesystem::path pdf = *xopp;
        pdf.replace_extension(L".pdf");
        auto doc = session.Document();
        std::wstring refused = Paths::Same(pdf, *source)
            ? std::format(L"Die neue PDF ({}) würde die importierte ersetzen.", pdf.filename().native())
            : doc && doc->PdfPath() && Paths::Same(pdf, *doc->PdfPath())
            ? std::format(L"{} ist die PDF des offenen Dokuments.", pdf.filename().native())
            : doc && doc->FilePath() && Paths::Same(*xopp, *doc->FilePath())
            ? std::format(L"{} ist das offene Dokument.", xopp->filename().native())
            : L"";
        if (!refused.empty())
        {
            message(L"Nicht importiert: " + refused + L" Bitte einen anderen Ordner oder Namen wählen.", ToastKind::Warning);
            return;
        }
        if (Paths::FileExists(pdf) && !Dialogs::Confirm(owner, L"PDF ersetzen?",
                std::format(L"{} gibt es dort schon. Die importierte PDF ohne Anmerkungen ersetzt sie.", pdf.filename().native()),
                L"Ersetzen"))
        {
            return;
        }

        running = true;
        Log(std::format("import: {} -> {}", ToUtf8(source->native()), ToUtf8(xopp->native())));
        UiQueue::RunInBackground("import PDF", [this, source = *source, xopp = *xopp, pdf]
        {
            // The new PDF is written beside its place first and takes it only when everything else worked: a failed
            // import leaves an existing PDF of that name as it was.
            std::filesystem::path staged = pdf.native() + L"~import";
            std::filesystem::path temp = std::filesystem::temp_directory_path() / (L"xournal-studio-import-" + NewGuidText());
            std::set<std::wstring> picturePdfs;
            std::wstring summary, error;
            try
            {
                auto start = std::chrono::steady_clock::now();
                auto import = import::Prepare(source, staged, [&](int round)
                {
                    std::filesystem::path file = temp.native() + std::format(L"-{}.pdf", round);
                    picturePdfs.insert(file.native());
                    return file;
                });
                import::RenderPictures(import, PicturePixelsPerPoint); // the thread pool's MTA, as Windows.Data.Pdf needs
                auto document = import::CreateDocument(import, pdf);
                if (!MoveFileExW(staged.c_str(), pdf.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    ThrowLastError(std::format("import: move {} into place", ToUtf8(staged.native())));
                }
                io::DocumentFile::Save(*document, xopp);
                summary = Summary(import, xopp);
                Log(std::format("import: done in {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count()));
            }
            catch (...)
            {
                LogException(std::format("import PDF {}", ToUtf8(source.native())));
                error = ErrorText();
            }
            DeleteTemporary(staged);
            for (auto const& file : picturePdfs) DeleteTemporary(file);
            UiQueue::Post("import PDF done", [this, xopp, summary, error]
            {
                running = false;
                if (!error.empty())
                {
                    message(L"Import fehlgeschlagen: " + error, ToastKind::Error);
                    return;
                }
                message(summary, ToastKind::Success);
                session.Open(xopp);
            });
        });
    }
}
