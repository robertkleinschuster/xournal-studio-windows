// Ported from XournalStudio.Tests/RecoveryTests.cs: copies of ended sessions are offered and restore the document;
// plus the autosave queue.

#include "TestSupport.h"

#include "xournal/base/Error.h"
#include "xournal/io/DocumentFile.h"
#include "xournal/model/Snapshot.h"
#include "xournal/recovery/Autosave.h"
#include "xournal/recovery/Recovery.h"

#include <gtest/gtest.h>

#include <atomic>

namespace xournal::tests
{
    using namespace xournal::recovery;
    using std::filesystem::path;

    namespace
    {
        constexpr DWORD NoSuchProcess = 0xFFFFFFF0; // process ids are multiples of 4

        RecoveryInfo Info(XoppDocument const& doc, ProcessStamp process)
        {
            RecoveryInfo info;
            info.documentPath = doc.FilePath();
            info.referenceBase = doc.ReferenceBase();
            info.pdfPath = doc.PdfPath();
            info.createdAt = DateTime{ 2026, 11, 15, 22, 5 };
            info.savedAt = std::chrono::file_clock::now();
            info.process = process;
            return info;
        }
    }

    TEST(Recovery, CopyOfAnEndedSessionIsOfferedAndRestoresTheDocument)
    {
        TempFolder temp;
        // A document next to its PDF, with unsaved changes (one more page than on disk).
        path docs = temp / L"Uni" / L"GET";
        std::filesystem::create_directories(docs);
        path pdf = docs / L"Skript.pdf";
        WriteBytes(pdf, "");
        auto doc = XoppDocument::FromPdf(pdf, { { 595, 842 }, { 595, 842 } });
        path file = docs / L"Skript.xopp";
        io::DocumentFile::Save(*doc, file);
        doc->Pages().push_back(XoppPage::CreateGraph(595, 842));

        Recovery recovery(Recovery::FolderIn(temp.Path()));
        RecoveryInfo info = Info(*doc, { NoSuchProcess, std::chrono::file_clock::now() });
        recovery.Write(L"a", *TakeSnapshot(*doc, info.referenceBase), info);

        auto entries = recovery.FindAbandoned();
        ASSERT_EQ(1u, entries.size());
        RecoveryEntry const& entry = entries[0];
        EXPECT_EQ(L"a", entry.id);
        EXPECT_EQ(file, entry.info.documentPath);
        EXPECT_EQ(info.savedAt, entry.info.savedAt);
        EXPECT_EQ(info.process, entry.info.process);
        EXPECT_EQ(info.createdAt, entry.info.createdAt);
        auto restored = io::DocumentFile::LoadCopy(entry.copyPath, entry.info.documentPath, entry.info.referenceBase);
        EXPECT_EQ(3u, restored->Pages().size());
        EXPECT_EQ(file, restored->FilePath());
        EXPECT_EQ(pdf, restored->PdfPath()); // the copy lies elsewhere, the reference still finds the PDF

        io::DocumentFile::Save(*restored, file);
        auto background = io::DocumentFile::ReadXml(file)->Element(L"page")->Element(L"background");
        ASSERT_NE(nullptr, background);
        EXPECT_EQ(L"Skript.pdf", background->AttributeOr(L"filename", L"<none>")); // still relative

        recovery.Delete(L"a");
        EXPECT_TRUE(recovery.FindAbandoned().empty());
    }

    TEST(Recovery, CopyOfARunningSessionIsNotOffered)
    {
        TempFolder temp;
        Recovery recovery(Recovery::FolderIn(temp.Path()));
        auto doc = XoppDocument::CreateNew();
        RecoveryInfo info = Info(*doc, ProcessStamp::Current());
        recovery.Write(L"b", *TakeSnapshot(*doc, info.referenceBase), info);

        EXPECT_TRUE(recovery.FindAbandoned().empty());
        // The same process id after a restart (another start time) is another session.
        info.process.start -= std::chrono::hours(1);
        recovery.Write(L"b", *TakeSnapshot(*doc, info.referenceBase), info);
        EXPECT_EQ(1u, recovery.FindAbandoned().size());
    }

    TEST(Recovery, NeverSavedDocumentOnAPdfRestoresWithItsPdf)
    {
        TempFolder temp;
        path pdf = temp / L"VO" / L"Folien 3.pdf";
        std::filesystem::create_directories(pdf.parent_path());
        WriteBytes(pdf, "");
        auto doc = XoppDocument::FromPdf(pdf, { { 595, 842 } });
        Recovery recovery(Recovery::FolderIn(temp.Path()));
        RecoveryInfo info = Info(*doc, { NoSuchProcess, std::chrono::file_clock::now() });
        recovery.Write(L"c", *TakeSnapshot(*doc, info.referenceBase), info);

        auto entries = recovery.FindAbandoned();
        ASSERT_EQ(1u, entries.size());
        EXPECT_EQ(std::nullopt, entries[0].info.documentPath);
        EXPECT_EQ(pdf, entries[0].info.pdfPath);
        auto restored = io::DocumentFile::LoadCopy(entries[0].copyPath, std::nullopt, entries[0].info.referenceBase);
        EXPECT_EQ(std::nullopt, restored->FilePath());
        EXPECT_EQ(pdf, restored->PdfPath());
        EXPECT_EQ(L"Folien 3", restored->SuggestedName());
    }

    TEST(Recovery, DeletingWithoutAnyCopyWrittenIsFine)
    {
        TempFolder temp;
        Recovery recovery(Recovery::FolderIn(temp.Path()));
        recovery.Delete(L"never-written");
        EXPECT_TRUE(recovery.FindAbandoned().empty());
    }

    TEST(Recovery, NewestFirstAndUnreadableInfoIsLeftAlone)
    {
        TempFolder temp;
        Recovery recovery(Recovery::FolderIn(temp.Path()));
        auto doc = XoppDocument::CreateNew();
        RecoveryInfo older = Info(*doc, { NoSuchProcess, std::chrono::file_clock::now() });
        older.savedAt -= std::chrono::minutes(5);
        RecoveryInfo newer = Info(*doc, { NoSuchProcess, std::chrono::file_clock::now() });
        recovery.Write(L"older", *TakeSnapshot(*doc, older.referenceBase), older);
        recovery.Write(L"newer", *TakeSnapshot(*doc, newer.referenceBase), newer);
        WriteBytes(recovery.Folder() / L"broken.json", "{ nicht json");
        WriteBytes(recovery.Folder() / L"broken.xopp", "x");

        auto entries = recovery.FindAbandoned();

        ASSERT_EQ(2u, entries.size());
        EXPECT_EQ(L"newer", entries[0].id);
        EXPECT_EQ(L"older", entries[1].id);
        EXPECT_TRUE(std::filesystem::exists(recovery.Folder() / L"broken.json")); // kept, never deleted
    }

    TEST(Recovery, NativeCopiesLiveApartFromTheCSharpApps)
    {
        EXPECT_EQ(path(L"C:\\x\\Recovery-native"), Recovery::FolderIn(L"C:\\x"));
        std::wstring id = Recovery::NewId();
        EXPECT_EQ(32u, id.size());
        EXPECT_NE(id, Recovery::NewId());
    }

    TEST(Autosave, WritesInOrderAndDeletesOnlyAfterAWrite)
    {
        TempFolder temp;
        Recovery recovery(Recovery::FolderIn(temp.Path()));
        auto doc = XoppDocument::CreateNew();
        std::atomic<int> failures = 0;
        bool snapshotFails = false;
        Autosave autosave(recovery, [&]
        {
            if (snapshotFails)
            {
                throw Error("snapshot failed", E_FAIL);
            }
            RecoveryInfo info = Info(*doc, { NoSuchProcess, std::chrono::file_clock::now() });
            return Autosave::Copy{ TakeSnapshot(*doc, info.referenceBase), info };
        }, [&](std::string const&, std::exception_ptr) { failures++; });

        autosave.WriteNow(); // nothing pending: nothing written
        autosave.WaitIdle();
        EXPECT_TRUE(recovery.FindAbandoned().empty());

        autosave.Changed();
        EXPECT_TRUE(autosave.Pending());
        autosave.WriteNow();
        EXPECT_FALSE(autosave.Pending());
        autosave.WaitIdle();
        auto written = recovery.FindAbandoned();
        ASSERT_EQ(1u, written.size());
        EXPECT_EQ(autosave.Id(), written[0].id);

        // A failed snapshot is reported and stays pending.
        snapshotFails = true;
        autosave.Changed();
        autosave.WriteNow();
        EXPECT_EQ(1, failures.load());
        EXPECT_TRUE(autosave.Pending());
        snapshotFails = false;

        // Restored copy replaced by the new one.
        std::wstring old = autosave.Id();
        autosave.StartNewDocument(); // the old copy goes
        autosave.Changed();
        autosave.WriteNow();
        autosave.WaitIdle();
        written = recovery.FindAbandoned();
        ASSERT_EQ(1u, written.size());
        EXPECT_EQ(autosave.Id(), written[0].id);
        EXPECT_NE(old, autosave.Id());

        std::wstring restored = autosave.Id();
        autosave.StartNewDocument();
        autosave.WaitIdle(); // its delete is done before the restored copy is written again below
        recovery.Write(restored, *TakeSnapshot(*doc, doc->ReferenceBase()), Info(*doc, { NoSuchProcess, std::chrono::file_clock::now() }));
        autosave.WriteNowReplacing(restored);
        autosave.WaitIdle();
        written = recovery.FindAbandoned();
        ASSERT_EQ(1u, written.size());
        EXPECT_EQ(autosave.Id(), written[0].id);

        autosave.Discard();
        autosave.WaitIdle();
        EXPECT_TRUE(recovery.FindAbandoned().empty());
        EXPECT_EQ(1, failures.load());
    }
}
