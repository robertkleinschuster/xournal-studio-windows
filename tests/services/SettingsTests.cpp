// The native app's settings (settings-native.json): defaults, round trip, unknown keys kept, damaged files left alone.
// (XournalStudio.Tests/AppSettingsTests.cs tested the C# settings' migration of flat tool fields; the native app keeps
// InkPages' ToolChoices as its serialized string, so there is nothing to migrate.)

#include "TestSupport.h"

#include "xournal/settings/Settings.h"

#include <gtest/gtest.h>

namespace xournal::tests
{
    using xournal::settings::Settings;

    TEST(Settings, MissingFileGivesDefaults)
    {
        TempFolder temp;
        Settings settings = Settings::Load(Settings::FileIn(temp.Path()));
        EXPECT_EQ(temp / L"settings-native.json", settings.File());
        EXPECT_FALSE(settings.window);
        EXPECT_FALSE(settings.projectFolder);
        EXPECT_FALSE(settings.paneOpen);
        EXPECT_FALSE(settings.doneTasksShown);
        EXPECT_TRUE(settings.toolChoices.empty());
        EXPECT_TRUE(settings.documentPositions.empty());
    }

    TEST(Settings, EverythingComesBackAsSaved)
    {
        TempFolder temp;
        auto file = Settings::FileIn(temp / L"neu"); // the folder is created
        Settings settings = Settings::Load(file);
        settings.window = settings::WindowState{ { 10, 20, 810, 620 }, false };
        settings.lastFile = temp / L"\u00DCbung 1.xopp";
        settings.projectFolder = temp.Path();
        settings.paneOpen = true;
        settings.paneTab = 2;
        settings.doneTasksShown = true;
        settings.toolChoices = "pens=1:0.85,3:2.26;eraser=7";
        settings.documentPositions[L"C:\\Uni\\Blatt.xopp"] = { 1234.5, 1.5 };
        settings.Save();

        Settings read = Settings::Load(file);

        ASSERT_TRUE(read.window);
        EXPECT_EQ(10, read.window->normal.left);
        EXPECT_EQ(620, read.window->normal.bottom);
        EXPECT_FALSE(read.window->maximized);
        EXPECT_EQ(settings.lastFile, read.lastFile);
        EXPECT_EQ(settings.projectFolder, read.projectFolder);
        EXPECT_TRUE(read.paneOpen);
        EXPECT_EQ(2, read.paneTab);
        EXPECT_TRUE(read.doneTasksShown);
        EXPECT_EQ(settings.toolChoices, read.toolChoices);
        // Positions by path, case ignored like Windows.
        ASSERT_TRUE(read.documentPositions.contains(L"c:\\uni\\blatt.xopp"));
        EXPECT_EQ((settings::DocumentPosition{ 1234.5, 1.5 }), read.documentPositions.at(L"C:\\UNI\\BLATT.XOPP"));

        read.projectFolder.reset();
        read.Save();
        EXPECT_FALSE(Settings::Load(file).projectFolder);
    }

    TEST(Settings, KeysOfANewerVersionAreKept)
    {
        TempFolder temp;
        auto file = Settings::FileIn(temp.Path());
        WriteBytes(file, R"({"paneOpen":true,"future":{"a":[1,2]},"lastFile":"C:\\x.xopp"})");

        Settings settings = Settings::Load(file);
        EXPECT_TRUE(settings.paneOpen);
        settings.paneOpen = false;
        settings.Save();

        std::string text = ReadText(file);
        EXPECT_NE(text.find(R"("future":{"a":[1,2]})"), std::string::npos) << text;
        EXPECT_FALSE(Settings::Load(file).paneOpen);
        EXPECT_EQ(std::filesystem::path(L"C:\\x.xopp"), Settings::Load(file).lastFile);
    }

    TEST(Settings, ADamagedFileGivesDefaultsAndStaysUntilSaved)
    {
        TempFolder temp;
        auto file = Settings::FileIn(temp.Path());
        WriteBytes(file, "{ kaputt");

        Settings settings = Settings::Load(file);

        EXPECT_FALSE(settings.paneOpen);
        EXPECT_EQ("{ kaputt", ReadText(file));
    }

    TEST(Settings, TheCSharpAppsSettingsAreNeverRead)
    {
        TempFolder temp;
        WriteBytes(temp / L"settings.json", R"({"ProjectFolder":"C:\\work","ExplorerOpen":true})");
        Settings settings = Settings::Load(Settings::FileIn(temp.Path()));
        EXPECT_FALSE(settings.projectFolder);
        settings.Save();
        EXPECT_EQ(R"({"ProjectFolder":"C:\\work","ExplorerOpen":true})", ReadText(temp / L"settings.json"));
    }
}
