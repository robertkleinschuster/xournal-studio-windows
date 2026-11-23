# Unit tests of base, model, io and pdf (ported from XournalStudio.Tests, same sample XML). The model tests parse their
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
    FormatCompatibilityTests.cpp
    ExampleTests.cpp
    PdfTestSupport.cpp
    PdfExportTests.cpp
    PdfDocumentExportTests.cpp
    PdfPageAppenderTests.cpp
    ClipTests.cpp
    PdfNavigationTests.cpp
    PdfPlacesTests.cpp
    RenderTilesTests.cpp
    DarkPdfTests.cpp
    ../app/src/DarkPdf.cpp
)
target_compile_options(XournalTests PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_compile_definitions(XournalTests PRIVATE XOURNAL_EXAMPLES_DIR=L"${CMAKE_SOURCE_DIR}/examples")
# The pdf tests build source PDFs and inspect results with qpdf, encode pictures with WIC and open results with
# Windows.Data.Pdf.
target_include_directories(XournalTests PRIVATE ../app/src ../external/InkPages/include)
target_link_libraries(XournalTests PRIVATE XournalIo XournalPdf qpdf::libqpdf GTest::gtest_main ole32 crypt32 windowscodecs windowsapp d2d1 dxguid)
include(GoogleTest)
gtest_discover_tests(XournalTests)
add_subdirectory(services)
add_subdirectory(import)
add_subdirectory(pdftext)
add_subdirectory(search)
