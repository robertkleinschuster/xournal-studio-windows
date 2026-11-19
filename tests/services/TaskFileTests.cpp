// Ported from XournalStudio.Tests/TodoTxtTests.cs (the TaskFile tests): line edits keep the file's format.

#include "TestSupport.h"

#include "xournal/base/Error.h"
#include "xournal/tasks/TaskFile.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    using xournal::tasks::TaskFile;

    TEST(TaskFile, ChangesTouchOnlyTheirLineAndKeepTheFormat)
    {
        TempFolder temp;
        auto path = temp / L"todo.txt";
        WriteBytes(path, "a\r\nb\r\nc"); // CRLF, no final newline
        TaskFile file(path);

        EXPECT_TRUE(file.Replace(L"b", L"x 2026-11-15 b"));
        EXPECT_FALSE(file.Replace(L"b", L"never")); // already changed (e.g. by another app)
        file.Append(L"d");
        EXPECT_TRUE(file.Remove(L"a"));
        EXPECT_FALSE(file.Remove(L"a"));
        EXPECT_EQ("x 2026-11-15 b\r\nc\r\nd", ReadText(path));
        EXPECT_FALSE(std::filesystem::exists(path.native() + L"~"));

        auto fresh = temp / L"neu" / L"todo.txt";
        std::filesystem::create_directories(fresh.parent_path());
        EXPECT_FALSE(TaskFile(fresh).Exists());
        EXPECT_TRUE(TaskFile(fresh).ReadLines().empty());
        TaskFile(fresh).Append(L"2026-11-15 erste");
        EXPECT_EQ("2026-11-15 erste\n", ReadText(fresh));
        EXPECT_EQ(std::vector<std::wstring>{ L"2026-11-15 erste" }, TaskFile(fresh).ReadLines());
    }

    TEST(TaskFile, EachChangeReadsTheFileAgain)
    {
        TempFolder temp;
        auto path = temp / L"todo.txt";
        WriteBytes(path, "a\n");
        TaskFile file(path);
        WriteBytes(path, "a\nvon woanders\n"); // a git pull or another app wrote meanwhile

        file.Append(L"b");

        EXPECT_EQ("a\nvon woanders\nb\n", ReadText(path));
    }

    TEST(TaskFile, ByteOrderMarkStays)
    {
        TempFolder temp;
        auto path = temp / L"bom.txt";
        WriteBytes(path, "\xEF\xBB\xBF" "a\n");
        TaskFile(path).Replace(L"a", L"ä");
        EXPECT_EQ("\xEF\xBB\xBF" "\xC3\xA4\n", ReadText(path));
    }

    TEST(TaskFile, Utf16StaysAndAnsiIsRefusedUnchanged)
    {
        TempFolder temp;
        auto utf16 = temp / L"u16.txt";
        std::wstring text = L"﻿GET HÜ\r\n";
        WriteBytes(utf16, std::string_view(reinterpret_cast<char const*>(text.data()), text.size() * 2));
        EXPECT_TRUE(TaskFile(utf16).Replace(L"GET HÜ", L"x GET HÜ"));
        std::wstring expected = L"﻿x GET HÜ\r\n";
        EXPECT_EQ(std::string(reinterpret_cast<char const*>(expected.data()), expected.size() * 2), ReadText(utf16));

        auto ansi = temp / L"ansi.txt";
        std::string latin1 = "H\xDC\n"; // "HÜ" in Windows-1252
        WriteBytes(ansi, latin1);
        try
        {
            TaskFile(ansi).Replace(L"x", L"y");
            FAIL() << "an ANSI file was accepted";
        }
        catch (Error const& e)
        {
            EXPECT_EQ(HRESULT_FROM_WIN32(ERROR_INVALID_DATA), e.Code());
        }
        EXPECT_EQ(latin1, ReadText(ansi));
    }

    TEST(TaskFile, EmptyAndSingleLineFiles)
    {
        TempFolder temp;
        auto empty = temp / L"empty.txt";
        WriteBytes(empty, "");
        EXPECT_TRUE(TaskFile(empty).ReadLines().empty());
        TaskFile(empty).Append(L"a");
        EXPECT_EQ("a\n", ReadText(empty));

        auto blank = temp / L"blank.txt";
        WriteBytes(blank, "\n");
        EXPECT_EQ(std::vector<std::wstring>{ L"" }, TaskFile(blank).ReadLines());
    }
}
