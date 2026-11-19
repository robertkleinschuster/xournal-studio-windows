# Unit tests of the services (ported from XournalStudio.Tests: ExplorerTests, RecoveryTests, TodoTxtTests' TaskFile
# part), one executable of their own so libgit2 stays out of the other tests. Its main runs the tests in a
# single-threaded apartment, as the app's UI thread does (IFileOperation, Windows.Data.Json).
add_executable(XournalServicesTests
    ServicesTestMain.cpp
    ../TestSupport.cpp
    ProjectTests.cpp
    GitTests.cpp
    TaskFileTests.cpp
    RecoveryTests.cpp
    SettingsTests.cpp
)
target_include_directories(XournalServicesTests PRIVATE ..)
target_compile_options(XournalServicesTests PRIVATE ${XOURNAL_COMPILE_OPTIONS})
# The git tests set up their repositories (bare remote, clones, first commit) with libgit2 itself.
target_link_libraries(XournalServicesTests PRIVATE XournalProject XournalGit XournalTasks XournalRecovery
    XournalSettings xournal_libgit2 GTest::gtest ole32 windowsapp)
include(GoogleTest)
gtest_discover_tests(XournalServicesTests)
