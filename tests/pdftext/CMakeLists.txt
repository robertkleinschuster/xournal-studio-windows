# Unit tests of the PDF text module (PDFium), one executable of their own: pdfium.dll (and its licenses) is put next
# to it, as next to the app. Synthetic PDFs built with qpdf.
add_executable(XournalPdfTextTests
    ../TestSupport.cpp
    PdfTextTests.cpp
)
target_include_directories(XournalPdfTextTests PRIVATE ..)
target_compile_options(XournalPdfTextTests PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalPdfTextTests PRIVATE XournalPdfText qpdf::libqpdf GTest::gtest_main)
xournal_deploy_pdfium(XournalPdfTextTests)
include(GoogleTest)
gtest_discover_tests(XournalPdfTextTests)
