#include "RecoveryCopies.h"

#include "ErrorText.h"
#include "Questions.h"
#include "UiQueue.h"

#include "xournal/base/Error.h"
#include "xournal/base/Log.h"
#include "xournal/base/Paths.h"
#include "xournal/base/Text.h"
#include "xournal/io/DocumentFile.h"
#include "xournal/model/Snapshot.h"

#include <format>

namespace xournal::app
{
    namespace
    {
        constexpr auto CopyDelay = std::chrono::seconds(5);

        SYSTEMTIME LocalTime(recovery::Time time)
        {
            auto ticks = static_cast<std::uint64_t>(time.time_since_epoch().count());
            FILETIME utc{ static_cast<DWORD>(ticks & 0xFFFFFFFF), static_cast<DWORD>(ticks >> 32) };
            SYSTEMTIME utcParts{}, local{};
            if (!FileTimeToSystemTime(&utc, &utcParts) || !SystemTimeToTzSpecificLocalTime(nullptr, &utcParts, &local))
            {
                ThrowLastError("recovery: local time");
            }
            return local;
        }

        std::wstring LocalText(recovery::Time time)
        {
            SYSTEMTIME t = LocalTime(time);
            return std::format(L"{:02}.{:02}.{:04} {:02}:{:02}", t.wDay, t.wMonth, t.wYear, t.wHour, t.wMinute);
        }

        // The document's name without extension (also the name of the copy in the recycle bin).
        std::wstring BaseName(recovery::RecoveryInfo const& info)
        {
            DateTime created = info.createdAt.value_or([&]
            {
                SYSTEMTIME t = LocalTime(info.savedAt);
                return DateTime{ t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute };
            }());
            return DocumentNames::NameFor(info.documentPath, info.pdfPath, created);
        }

        std::optional<recovery::Time> LastWrite(std::filesystem::path const& file)
        {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data))
            {
                return std::nullopt;
            }
            auto ticks = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
            return recovery::Time(recovery::Time::duration(static_cast<recovery::Time::rep>(ticks)));
        }

        std::wstring Describe(recovery::RecoveryInfo const& info)
        {
            std::wstring name = info.documentPath ? info.documentPath->filename().native() : BaseName(info) + L" (nie gespeichert)";
            std::wstring text = std::format(L"„{}“ wurde nicht ordnungsgemäß geschlossen. Die nicht gespeicherten "
                L"Änderungen sind gesichert (Stand {}).", name, LocalText(info.savedAt));
            if (info.documentPath && !Paths::FileExists(*info.documentPath))
            {
                text += std::format(L"\n\nDie Datei {} gibt es nicht mehr; beim Speichern wird nach dem Ort gefragt.",
                    info.documentPath->native());
            }
            else if (info.documentPath)
            {
                auto written = LastWrite(*info.documentPath);
                if (written && *written > info.savedAt)
                {
                    text += std::format(L"\n\nAchtung: Die Datei wurde danach noch geändert ({}). Wiederherstellen und "
                        L"Speichern überschreibt diesen Stand.", LocalText(*written));
                }
            }
            return text;
        }
    }

    RecoveryCopies::RecoveryCopies(HWND window, std::filesystem::path const& appFolder, DocumentSession& documentSession,
        DocumentView& documentView, Messages show)
        : owner(window), session(documentSession), view(documentView), messages(std::move(show)),
          store(recovery::Recovery::FolderIn(appFolder)), process(recovery::ProcessStamp::Current()),
          delayed("recovery copy", CopyDelay, [this] { autosave->WriteNow(); })
    {
        autosave = std::make_unique<recovery::Autosave>(store, [this] { return TakeCopy(); },
            [this](std::string const&, std::exception_ptr error)
            {
                std::wstring text = std::format(L"Wiederherstellungskopie fehlgeschlagen: {}", ErrorText(error));
                UiQueue::Post("recovery: report failure", [this, text] { messages(text, MessageKind::Warning); });
            });
        // Windows starts the app again after a crash or an update restart (if it ran 60 s); the start offers the copy.
        HRESULT hr = RegisterApplicationRestart(L"", 0);
        if (FAILED(hr))
        {
            LogError(std::format("recovery: RegisterApplicationRestart: {}", DescribeHr(hr)));
        }
    }

    RecoveryCopies::~RecoveryCopies()
    {
        delayed.Cancel();
        autosave.reset(); // finishes what is queued
    }

    recovery::Autosave::Copy RecoveryCopies::TakeCopy()
    {
        auto doc = session.Document();
        recovery::Autosave::Copy copy;
        copy.info.documentPath = doc->FilePath();
        copy.info.referenceBase = doc->ReferenceBase();
        copy.info.pdfPath = doc->PdfPath();
        copy.info.createdAt = doc->CreatedAt();
        copy.info.savedAt = std::chrono::file_clock::now();
        copy.info.process = process;
        view.CaptureInk([&] { copy.snapshot = TakeSnapshot(*doc, copy.info.referenceBase); });
        return copy;
    }

    void RecoveryCopies::Update()
    {
        auto doc = session.Document();
        if (doc.get() != shown)
        {
            // Another document: the previous one was left for good (saved or discarded), so its copy goes.
            delayed.Cancel();
            autosave->StartNewDocument();
            shown = doc.get();
            copyWanted = false;
        }
        if (session.IsModified())
        {
            autosave->Changed();
            delayed.Start(); // 5 s after the first change, not after the last
            copyWanted = true;
        }
        else if (copyWanted)
        {
            delayed.Cancel();
            autosave->Discard(); // saved: nothing to recover
            copyWanted = false;
        }
    }

    void RecoveryCopies::Deactivated()
    {
        if (session.IsModified())
        {
            delayed.Cancel();
            autosave->WriteNow();
        }
    }

    void RecoveryCopies::End()
    {
        delayed.Cancel();
        autosave->Discard();
        autosave->WaitIdle();
    }

    bool RecoveryCopies::OfferAtStart()
    {
        std::vector<recovery::RecoveryEntry> entries;
        try
        {
            entries = store.FindAbandoned();
        }
        catch (...)
        {
            LogException("recovery: find copies");
            messages(std::format(L"Wiederherstellungskopien nicht lesbar: {}", ErrorText()), MessageKind::Error);
            return false;
        }
        for (auto const& entry : entries)
        {
            if (Offer(entry))
            {
                return true;
            }
        }
        return false;
    }

    // True: restored. Verwerfen recycles the copy, Später keeps it for the next start.
    bool RecoveryCopies::Offer(recovery::RecoveryEntry const& entry)
    {
        int choice = Questions::Choose(owner, L"Nicht gespeicherte Änderungen wiederherstellen?", Describe(entry.info),
            { L"Wiederherstellen", L"Verwerfen", L"Später" }, 0);
        if (choice == 1)
        {
            try
            {
                store.Recycle(entry, BaseName(entry.info), owner);
            }
            catch (...)
            {
                LogException(std::format("recovery: recycle {}", ToUtf8(entry.id)));
                messages(std::format(L"Verwerfen fehlgeschlagen: {}", ErrorText()), MessageKind::Error);
            }
            return false;
        }
        if (choice != 0)
        {
            return false;
        }
        try
        {
            auto const& info = entry.info;
            std::optional<std::filesystem::path> file;
            if (info.documentPath && Paths::FileExists(*info.documentPath))
            {
                file = info.documentPath;
            }
            std::shared_ptr<XoppDocument> doc = io::DocumentFile::LoadCopy(entry.copyPath, file, info.referenceBase);
            session.ShowRecovered(doc);
            Update();
            autosave->WriteNowReplacing(entry.id); // the restored state is this session's copy now
            return true;
        }
        catch (...)
        {
            LogException(std::format("recovery: restore {}", ToUtf8(entry.id)));
            messages(std::format(L"Wiederherstellung fehlgeschlagen: {} – die Kopie bleibt erhalten: {}", ErrorText(),
                entry.copyPath.native()), MessageKind::Error);
            return false;
        }
    }
}
