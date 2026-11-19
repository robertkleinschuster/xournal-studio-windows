# Unit tests of the iPad import (ported from XournalStudio.Tests/MarkupImportTests.cs), one executable of their own:
# synthetic inputs only - protobuf written with protozero, binary plists with a small writer, PDFs with qpdf; the
# pictures are rendered with Windows.Data.Pdf (multithreaded apartment: gtest_main's thread).
add_executable(XournalImportTests
    ../TestSupport.cpp
    ../PdfTestSupport.cpp
    ImportTests.cpp
)
target_include_directories(XournalImportTests PRIVATE ..)
target_compile_options(XournalImportTests PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalImportTests PRIVATE XournalImport libqpdf protozero GTest::gtest_main ole32 crypt32 windowscodecs windowsapp)
include(GoogleTest)
gtest_discover_tests(XournalImportTests)
