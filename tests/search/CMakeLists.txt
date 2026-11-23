# Unit tests of the document search, one executable of their own (PDFium next to it, as next to the app).
add_executable(XournalSearchTests
    ../TestSupport.cpp
    SearchTests.cpp
)
target_include_directories(XournalSearchTests PRIVATE ..)
target_compile_options(XournalSearchTests PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalSearchTests PRIVATE XournalSearch XournalIo qpdf::libqpdf GTest::gtest_main dwrite)
xournal_deploy_pdfium(XournalSearchTests)
include(GoogleTest)
gtest_discover_tests(XournalSearchTests)
