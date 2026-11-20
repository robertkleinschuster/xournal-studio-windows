// Ported from XournalStudio.Tests/TodoTxtTests.cs (the TaskFile tests belong to the later tasks service).

#include "TestSupport.h"

#include "xournal/model/TodoTxt.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    namespace
    {
        constexpr Date Today{ 2026, 11, 15 };
    }

    class TodoLinesAreWrittenBackAsTheyWere : public testing::TestWithParam<wchar_t const*> {};

    TEST_P(TodoLinesAreWrittenBackAsTheyWere, Line)
    {
        EXPECT_EQ(GetParam(), TodoTxt::Parse(GetParam())->ToString());
    }

    INSTANTIATE_TEST_SUITE_P(TodoTxt, TodoLinesAreWrittenBackAsTheyWere, testing::Values(
        L"x 2026-11-05 2026-11-01 GET HÜ Abgabe due:2026-11-05 time:18:00",
        L"2026-11-04 Knotenspannungsverfahren Klassisch (Teilklausur)",
        L"x 2026-11-01 Vorbereitung Mathe A VO bzgl. Funktionen  due:2026-10-28",
        L"(A) 2026-11-06 Polynomdivision +Mathe @uni",
        L"Ohne Datum"));

    TEST(TodoTxt, ReadsDatesTagsAndTitle)
    {
        auto task = *TodoTxt::Parse(L"x 2026-11-05 2026-11-01 GET HÜ Abgabe due:2026-11-05 time:18:00 siehe https://x.at/a");
        EXPECT_TRUE(task.done);
        EXPECT_EQ((Date{ 2026, 11, 5 }), task.completed);
        EXPECT_EQ((Date{ 2026, 11, 1 }), task.created);
        EXPECT_EQ((Date{ 2026, 11, 5 }), task.Due());
        EXPECT_EQ((TimeOfDay{ 18, 0 }), task.Time());
        EXPECT_EQ(L"GET HÜ Abgabe siehe https://x.at/a", task.Title());
        EXPECT_FALSE(TodoTxt::Parse(L"   "));
    }

    TEST(TodoTxt, CompletingMovesThePriorityIntoATagAndReopeningBringsItBack)
    {
        auto task = *TodoTxt::Parse(L"(B) 2026-11-06 Polynomdivision due:2026-11-20");
        auto done = task.Complete(Today);
        EXPECT_EQ(L"x 2026-11-15 2026-11-06 Polynomdivision due:2026-11-20 pri:B", done.ToString());
        EXPECT_EQ(L"(B) 2026-11-06 Polynomdivision due:2026-11-20", done.Reopen().ToString());
    }

    TEST(TodoTxt, NewTaskHasTodaysDateAndItsDueDate)
    {
        EXPECT_EQ(L"2026-11-15 GET Altklausur rechnen due:2026-11-20",
            TodoTxt::Create(L" GET Altklausur rechnen ", Today, Date{ 2026, 11, 20 }).ToString());
        EXPECT_EQ(L"a due:2026-12-01 b", TodoTxt::SetTag(L"a due:2026-11-01 b", L"due", L"2026-12-01"));
        EXPECT_EQ(L"a b", TodoTxt::SetTag(L"a due:2026-11-01 b", L"due", std::nullopt));
    }

    TEST(TodoTxt, DocumentLinksAreRelativeEncodedAndResolve)
    {
        TempFolder temp;
        auto todo = temp / L"todo.txt";
        auto doc = temp.Path() / L"GET" / L"UE 3" / L"Blatt#1.xopp";
        auto link = TodoTxt::DocumentLink(todo, doc, 2);
        EXPECT_EQ(L"GET/UE%203/Blatt%231.xopp#p3", link);
        auto resolved = TodoTxt::ResolveDocumentLink(todo, link);
        ASSERT_TRUE(resolved);
        EXPECT_EQ(doc, resolved->first);
        EXPECT_EQ(2, resolved->second);
        auto withoutPage = TodoTxt::ResolveDocumentLink(todo, L"GET/UE%203/Blatt%231.xopp");
        EXPECT_EQ(doc, withoutPage->first);
        EXPECT_EQ(-1, withoutPage->second);
        EXPECT_EQ(L"Ü.xopp", TodoTxt::ResolveDocumentLink(todo, L"%C3%9C.xopp")->first.filename().native()); // UTF-8 escapes
    }

    TEST(TodoTxt, TagValuesAreLiteral)
    {
        EXPECT_EQ(L"a doc:%C3%9C$_.xopp b", TodoTxt::SetTag(L"a doc:old b", L"doc", L"%C3%9C$_.xopp"));
    }

    TEST(TodoTxt, TimesInTheTextAreNotTags)
    {
        auto task = *TodoTxt::Parse(L"2026-11-15 Prüfung GET 10:30 Hörsaal i13 due:2026-11-20");
        EXPECT_EQ(L"Prüfung GET 10:30 Hörsaal i13", task.Title());
        EXPECT_FALSE(task.Tag(L"10"));
    }

    // The in-place edit: starts with the priority and without due:/doc:, reads the priority back the todo.txt way.
    TEST(TodoTxt, EditTextCarriesThePriorityAndKeepsDueAndLink)
    {
        auto task = *TodoTxt::Parse(L"(A) 2026-11-06 Blatt 3 +Mathe due:2026-11-20 doc:Blatt%203.xopp#p2");
        EXPECT_EQ(L"(A) Blatt 3 +Mathe", TodoTxt::EditText(task));

        EXPECT_EQ(L"(B) 2026-11-06 Blatt 4 +Mathe due:2026-11-20 doc:Blatt%203.xopp#p2",
            TodoTxt::WithEditText(task, L"(B) Blatt 4 +Mathe").ToString());
        EXPECT_EQ(L"2026-11-06 Blatt 4 due:2026-11-20 doc:Blatt%203.xopp#p2",
            TodoTxt::WithEditText(task, L"  Blatt 4 ").ToString());
        EXPECT_EQ(L"(C) 2026-11-06 Neu due:2026-11-20 doc:Blatt%203.xopp#p2",
            TodoTxt::WithEditText(*TodoTxt::Parse(L"2026-11-06 Alt due:2026-11-20 doc:Blatt%203.xopp#p2"), L"(C) Neu").ToString());
        // Unchanged text: the same line.
        EXPECT_EQ(task.ToString(), TodoTxt::WithEditText(task, TodoTxt::EditText(task)).ToString());
    }

    TEST(TodoTxt, EditTextOfADoneTaskIsTakenAsWritten)
    {
        auto task = *TodoTxt::Parse(L"x 2026-11-10 2026-11-06 Blatt 3 pri:A");
        EXPECT_EQ(L"Blatt 3 pri:A", TodoTxt::EditText(task));
        EXPECT_EQ(L"x 2026-11-10 2026-11-06 (B) Blatt 4", TodoTxt::WithEditText(task, L"(B) Blatt 4").ToString());
    }
}
