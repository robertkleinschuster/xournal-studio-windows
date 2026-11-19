# Unit tests of base, model and io (ported from XournalStudio.Tests, same sample XML). The model tests parse their
# sample XML with io's reader, so one test executable links both.
add_executable(XournalTests
    TestSupport.cpp
    XmlTests.cpp
    FileFormatTests.cpp
    ReferenceTests.cpp
    SnapshotTests.cpp
    ShapeAndMoveTests.cpp
    PictureTests.cpp
    TextTests.cpp
    StrokeTests.cpp
    TodoTxtTests.cpp
    CopyPagesTests.cpp
    ExampleTests.cpp
)
target_compile_options(XournalTests PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_compile_definitions(XournalTests PRIVATE XOURNAL_EXAMPLES_DIR=L"${CMAKE_SOURCE_DIR}/../examples")
target_link_libraries(XournalTests PRIVATE XournalIo GTest::gtest_main ole32)
include(GoogleTest)
gtest_discover_tests(XournalTests)
