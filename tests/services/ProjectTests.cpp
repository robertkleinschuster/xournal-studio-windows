// Ported from XournalStudio.Tests/ExplorerTests.cs: the tree, names, renaming, the folder watcher.

#include "TestSupport.h"

#include "xournal/base/Error.h"
#include "xournal/base/Paths.h"
#include "xournal/project/FileClipboard.h"
#include "xournal/project/FolderWatcher.h"
#include "xournal/project/ProjectFiles.h"
#include "xournal/project/ProjectTree.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <cstring>
#include <chrono>
#include <thread>

namespace xournal::tests
{
    using namespace xournal::project;
    using std::filesystem::path;

    namespace
    {
        path Touch(TempFolder const& temp, std::wstring const& relative, std::string_view content = "x")
        {
            path file = temp.Path() / relative;
            std::filesystem::create_directories(file.parent_path());
            WriteBytes(file, content);
            return file;
        }

        std::vector<std::wstring> Names(std::vector<ProjectEntry> const& entries)
        {
            std::vector<std::wstring> names;
            for (auto const& e : entries)
            {
                names.push_back(e.Name());
            }
            return names;
        }

        void CreateJunction(path const& link, path const& target)
        {
            std::wstring command = L"cmd.exe /c mklink /J \"" + link.native() + L"\" \"" + target.native() + L"\" >nul";
            STARTUPINFOW startup{ sizeof(startup) };
            PROCESS_INFORMATION process{};
            ASSERT_TRUE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                &startup, &process));
            WaitForSingleObject(process.hProcess, INFINITE);
            DWORD exitCode = 1;
            GetExitCodeProcess(process.hProcess, &exitCode);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            ASSERT_EQ(0u, exitCode);
        }
    }

    TEST(ProjectTree, ShowsEverythingOnDisk)
    {
        TempFolder temp;
        Touch(temp, L"GET/UE/blatt1.pdf");
        Touch(temp, L"GET/UE/blatt1.xopp");
        Touch(temp, L"GET/UE/blatt2_kommentiert.pdf");
        Touch(temp, L"GET/UE/notes.txt");
        Touch(temp, L"GET/UE/blatt4.xopp.bg.pdf");
        std::filesystem::create_directories(temp / L"Leer");
        // Only what Windows marks hidden is left out (git marks its .git folder hidden).
        Touch(temp, L".git/x.pdf");
        ASSERT_TRUE(SetFileAttributesW((temp / L".git").c_str(), FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN));
        // A junction back to the project folder is shown but not entered (endless recursion).
        path loop = temp / L"GET" / L"UE" / L"Schleife";
        CreateJunction(loop, temp.Path());

        ProjectEntry root = ProjectTree::Read(temp.Path());
        ASSERT_TRUE(RemoveDirectoryW(loop.c_str())); // removes the link only

        EXPECT_EQ((std::vector<std::wstring>{ L"GET", L"Leer" }), Names(root.children));
        ASSERT_EQ(1u, root.children[0].children.size());
        ProjectEntry const& ue = root.children[0].children[0];
        EXPECT_EQ((std::vector<std::wstring>{ L"Schleife", L"blatt1.pdf", L"blatt1.xopp", L"blatt2_kommentiert.pdf",
            L"blatt4.xopp.bg.pdf", L"notes.txt" }), Names(ue.children));
        EXPECT_TRUE(ue.children[0].isFolder);
        EXPECT_TRUE(ue.children[0].children.empty());
        EXPECT_TRUE(root.children[1].isFolder);
    }

    // One rule for "is this in that folder" across the explorer, git discard and moves (base's Paths).
    TEST(Paths, AtOrBelowMeansWholeNamesIgnoringCase)
    {
        EXPECT_TRUE(Paths::IsAtOrBelow(L"C:\\Uni\\UE\\blatt.xopp", L"C:\\Uni\\UE"));
        EXPECT_TRUE(Paths::IsAtOrBelow(L"C:\\Uni\\UE\\blatt.xopp", L"c:\\uni\\ue\\"));
        EXPECT_TRUE(Paths::IsAtOrBelow(L"C:\\Uni\\UE", L"C:\\Uni\\UE\\"));
        EXPECT_TRUE(Paths::IsAtOrBelow(L"C:\\Uni\\UE\\Teil\\a.pdf", L"C:\\Uni"));
        EXPECT_TRUE(Paths::IsAtOrBelow(L"C:\\Uni\\x", L"C:\\"));
        EXPECT_FALSE(Paths::IsAtOrBelow(L"C:\\Uni\\UEbung.xopp", L"C:\\Uni\\UE"));
        EXPECT_FALSE(Paths::IsAtOrBelow(L"C:\\Uni", L"C:\\Uni\\UE"));
        EXPECT_FALSE(Paths::IsAtOrBelow(L"D:\\Uni\\UE", L"C:\\Uni"));
        EXPECT_EQ(path(L"C:\\Uni\\UE"), Paths::WithoutTrailingSeparator(L"C:\\Uni\\UE\\\\"));
        EXPECT_EQ(path(L"C:\\Uni\\UE"), Paths::WithoutTrailingSeparator(L"C:/Uni/UE/"));
        EXPECT_EQ(path(L"C:\\"), Paths::WithoutTrailingSeparator(L"C:\\"));
    }

    TEST(ProjectTree, DeletedFilesAreAddedAtTheirPlace)
    {
        TempFolder temp;
        Touch(temp, L"UE/blatt1.xopp");
        Touch(temp, L"UE/blatt3.xopp");
        ProjectEntry root = ProjectTree::Read(temp.Path());

        std::wstring upper = temp.Path().native();
        for (wchar_t& c : upper) c = static_cast<wchar_t>(towupper(c)); // git may spell the root otherwise
        ProjectTree::AddMissing(root, { temp / L"UE" / L"blatt2.xopp", path(upper) / L"UE" / L"Alt" / L"weg.pdf",
            temp / L"UE" / L"blatt1.xopp", temp.Path().parent_path() / L"outside.xopp", temp / L"top.xopp" });

        EXPECT_EQ((std::vector<std::wstring>{ L"UE", L"top.xopp" }), Names(root.children));
        EXPECT_TRUE(root.children[1].missing);
        ProjectEntry const& ue = root.children[0];
        EXPECT_FALSE(ue.missing);
        EXPECT_EQ((std::vector<std::wstring>{ L"Alt", L"blatt1.xopp", L"blatt2.xopp", L"blatt3.xopp" }), Names(ue.children));
        EXPECT_TRUE(ue.children[0].isFolder);
        EXPECT_TRUE(ue.children[0].missing);
        EXPECT_EQ((std::vector<std::wstring>{ L"weg.pdf" }), Names(ue.children[0].children));
        EXPECT_FALSE(ue.children[1].missing); // on disk: listed once
        EXPECT_TRUE(ue.children[2].missing);
    }

    TEST(FileClipboard, DropBlockIsWhatFileExplorerReads)
    {
        std::vector<path> files{ L"C:\\Uni\\GET\\blatt1.xopp", L"C:\\Uni\\Übung 2" };
        std::vector<std::uint8_t> block = FileClipboard::DropBlock(files);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, block.size());
        ASSERT_NE(nullptr, memory);
        std::memcpy(GlobalLock(memory), block.data(), block.size());
        GlobalUnlock(memory);

        EXPECT_EQ(files, FileClipboard::FilesOf(static_cast<HDROP>(memory)));
        GlobalFree(memory);
    }

    TEST(ProjectFiles, TransferCopiesAndMovesLikeFileExplorer)
    {
        TempFolder temp;
        Touch(temp, L"UE/blatt1.xopp");
        Touch(temp, L"UE/Teil/a.pdf");
        std::filesystem::create_directories(temp / L"Ziel");

        auto copied = ProjectFiles::Transfer({ temp / L"UE" / L"blatt1.xopp", temp / L"UE" / L"Teil" }, temp / L"Ziel", false);
        ASSERT_EQ(2u, copied.size());
        EXPECT_TRUE(std::filesystem::exists(temp / L"Ziel" / L"blatt1.xopp"));
        EXPECT_TRUE(std::filesystem::exists(temp / L"Ziel" / L"Teil" / L"a.pdf"));
        EXPECT_TRUE(std::filesystem::exists(temp / L"UE" / L"blatt1.xopp")); // a copy leaves the original

        // A copy beside the original gets a new name; nothing is overwritten.
        auto beside = ProjectFiles::Transfer({ temp / L"UE" / L"blatt1.xopp" }, temp / L"UE", false);
        ASSERT_EQ(1u, beside.size());
        EXPECT_TRUE(Paths::Same(temp / L"UE" / L"blatt1.xopp", beside[0].from));
        EXPECT_FALSE(Paths::Same(beside[0].from, beside[0].to));
        EXPECT_TRUE(std::filesystem::exists(beside[0].to));

        auto moved = ProjectFiles::Transfer({ temp / L"UE" / L"Teil" }, temp.Path(), true);
        ASSERT_EQ(1u, moved.size());
        EXPECT_TRUE(Paths::Same(temp / L"Teil", moved[0].to));
        EXPECT_TRUE(std::filesystem::exists(temp / L"Teil" / L"a.pdf"));
        EXPECT_FALSE(std::filesystem::exists(temp / L"UE" / L"Teil"));
        // Moving into the folder it is in does nothing.
        EXPECT_TRUE(ProjectFiles::Transfer({ temp / L"Teil" }, temp.Path(), true).empty());
    }

    TEST(ProjectTree, NamesAreSortedAsFileExplorerSortsThem)
    {
        TempFolder temp;
        path folder = temp / L"VO";
        std::filesystem::create_directories(folder);
        for (wchar_t const* name : { L"Vorlesung 11.pdf", L"Vorlesung 2.pdf", L"vorlesung 1.pdf", L"Vorlesung 13.xopp", L"Vorlesung 12.pdf" })
        {
            WriteBytes(folder / name, "");
        }
        for (wchar_t const* name : { L"Übung 10", L"Übung 2", L"Übung 01" })
        {
            std::filesystem::create_directories(folder / name);
        }

        ProjectEntry root = ProjectTree::Read(temp.Path());

        ASSERT_EQ(1u, root.children.size());
        EXPECT_EQ((std::vector<std::wstring>{ L"Übung 01", L"Übung 2", L"Übung 10", // folders first, numbers by value
            L"vorlesung 1.pdf", L"Vorlesung 2.pdf", L"Vorlesung 11.pdf", L"Vorlesung 12.pdf", L"Vorlesung 13.xopp" }),
            Names(root.children[0].children));
        // Names equal but for case: ordinal order, so the order is stable.
        EXPECT_LT(ProjectTree::CompareNames(L"A.pdf", L"a.pdf"), 0);
        EXPECT_EQ(0, ProjectTree::CompareNames(L"a.pdf", L"a.pdf"));
    }

    TEST(ProjectTree, AFolderThatIsGoneIsShownEmpty)
    {
        TempFolder temp;
        ProjectEntry root = ProjectTree::Read(temp / L"gibt es nicht");
        EXPECT_TRUE(root.isFolder);
        EXPECT_TRUE(root.children.empty());
    }

    TEST(ProjectFiles, NamesAreCheckedLikeWindowsDoes)
    {
        EXPECT_FALSE(ProjectFiles::ValidateName(L"Blatt 3.xopp"));
        EXPECT_TRUE(ProjectFiles::ValidateName(L""));
        EXPECT_TRUE(ProjectFiles::ValidateName(L"  "));
        EXPECT_TRUE(ProjectFiles::ValidateName(L"a/b"));
        EXPECT_TRUE(ProjectFiles::ValidateName(L"a:b.xopp"));
        EXPECT_TRUE(ProjectFiles::ValidateName(L"Ordner."));
        EXPECT_TRUE(ProjectFiles::ValidateName(L"con.xopp"));
        EXPECT_FALSE(ProjectFiles::ValidateName(L"Console.xopp"));

        TempFolder temp;
        Touch(temp, L"Neues Dokument.xopp");
        Touch(temp, L"Neues Dokument (2).xopp");
        EXPECT_EQ(L"Neues Dokument (3).xopp", ProjectFiles::UniqueName(temp.Path(), L"Neues Dokument", L".xopp"));
        EXPECT_EQ(L"Neuer Ordner", ProjectFiles::UniqueName(temp.Path(), L"Neuer Ordner"));
    }

    TEST(ProjectFiles, RenamedPathsAreMapped)
    {
        TempFolder temp;
        path ue = temp / L"GET" / L"UE";
        path renamed = temp / L"GET" / L"Übung";
        EXPECT_TRUE(ProjectFiles::IsAtOrBelow(ue / L"a.xopp", ue));
        EXPECT_TRUE(ProjectFiles::IsAtOrBelow(ue, ue));
        EXPECT_TRUE(ProjectFiles::IsAtOrBelow(ue / L"a.xopp", ue.native() + L"\\")); // a separator at the end
        EXPECT_TRUE(ProjectFiles::IsAtOrBelow(temp / L"get" / L"ue" / L"a.xopp", ue)); // case ignored
        EXPECT_FALSE(ProjectFiles::IsAtOrBelow(temp / L"GET" / L"UE2" / L"a.xopp", ue));
        EXPECT_FALSE(ProjectFiles::IsAtOrBelow(std::nullopt, ue));
        EXPECT_EQ(renamed / L"a.xopp", ProjectFiles::MapRenamed(ue / L"a.xopp", ue, renamed));
        EXPECT_EQ(temp / L"b.xopp", ProjectFiles::MapRenamed(temp / L"b.xopp", ue, renamed));

        EXPECT_EQ(L"blatt1", ProjectFiles::EditableName(ue / L"blatt1.xopp", false));
        EXPECT_EQ(L"blatt2.xopp", ProjectFiles::NewName(ue / L"blatt1.xopp", false, L"blatt2"));
        EXPECT_EQ(L"UE", ProjectFiles::EditableName(ue, true));
        EXPECT_TRUE(ProjectFiles::SamePath(temp / L"x" / L".." / L"A.xopp", temp / L"a.xopp"));
        EXPECT_FALSE(ProjectFiles::SamePath(std::nullopt, temp / L"a.xopp"));
    }

    TEST(ProjectFiles, RenameRenamesOnlyThatFile)
    {
        TempFolder temp;
        path doc = Touch(temp, L"UE/blatt1.xopp", "doc");
        Touch(temp, L"UE/blatt1.xopp.bg.pdf", "pdf");
        Touch(temp, L"UE/blatt1.pdf", "lecture");

        path renamed = ProjectFiles::Rename(doc, L"Lösung 1.xopp");

        EXPECT_EQ(temp / L"UE" / L"Lösung 1.xopp", renamed);
        EXPECT_EQ("doc", ReadText(renamed));
        EXPECT_TRUE(std::filesystem::exists(temp / L"UE" / L"blatt1.xopp.bg.pdf"));
        EXPECT_TRUE(std::filesystem::exists(temp / L"UE" / L"blatt1.pdf"));
        EXPECT_FALSE(std::filesystem::exists(doc));
        EXPECT_THROW(ProjectFiles::Rename(renamed, L"blatt1.pdf"), Error); // never over another file
        EXPECT_EQ("lecture", ReadText(temp / L"UE" / L"blatt1.pdf"));
    }

    TEST(ProjectFiles, FoldersAreCreatedAndRenamed)
    {
        TempFolder temp;
        path created = ProjectFiles::CreateFolder(temp.Path(), L"Übung 1");
        Touch(temp, L"Übung 1/a.xopp");

        path renamed = ProjectFiles::Rename(created, L"Übung 2");

        EXPECT_EQ(temp / L"Übung 2", renamed);
        EXPECT_TRUE(std::filesystem::exists(renamed / L"a.xopp"));
        EXPECT_THROW(ProjectFiles::CreateFolder(temp.Path(), L"Übung 2"), Error);
    }

    TEST(FolderWatcher, ReportsChangesOutsideHiddenFolders)
    {
        TempFolder temp;
        std::filesystem::create_directories(temp / L".git");
        EXPECT_TRUE(FolderWatcher::IsInHiddenFolder(temp.Path(), temp / L".git" / L"index"));
        EXPECT_TRUE(FolderWatcher::IsInHiddenFolder(temp.Path(), temp / L".git"));
        EXPECT_FALSE(FolderWatcher::IsInHiddenFolder(temp.Path(), temp / L".gitignore"));
        EXPECT_FALSE(FolderWatcher::IsInHiddenFolder(temp.Path(), temp / L"UE" / L"a.xopp"));

        std::atomic<int> changes = 0;
        {
            FolderWatcher watcher(temp.Path(), [&] { changes++; });
            Touch(temp, L".git/index");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            EXPECT_EQ(0, changes.load());
            Touch(temp, L"UE/neu.xopp");
            for (int i = 0; i < 100 && changes == 0; i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            EXPECT_GT(changes.load(), 0);
        }
        // A handler that throws is logged; the watcher goes on and stops cleanly.
        std::atomic<int> calls = 0;
        {
            FolderWatcher watcher(temp.Path(), [&] { calls++; throw std::runtime_error("handler failed"); });
            Touch(temp, L"UE/zwei.xopp");
            for (int i = 0; i < 100 && calls == 0; i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            Touch(temp, L"UE/drei.xopp");
            for (int i = 0; i < 100 && calls < 2; i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            EXPECT_GE(calls.load(), 2);
        }
    }
}
