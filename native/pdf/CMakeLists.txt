# pdf: PDF export of a document (background PDF pages kept exactly, ink, pictures, texts) and appending pages of
# another PDF to the document's PDF. qpdf does the PDF structure (page import, annotation flattening, writing);
# Windows does text (DirectWrite, Font Subsetting API) and pictures (WIC). Depends on io (and so on model and base):
# the export draws the model's StrokeShape parts and BackgroundRuling - model never depends on pdf.
include(Dependencies.cmake)

add_library(XournalPdf STATIC
    src/ContentStream.cpp
    src/FontTables.cpp
    src/Fonts.cpp
    src/TextLayout.cpp
    src/TextDrawing.cpp
    src/Images.cpp
    src/QpdfSources.cpp
    src/PdfExport.cpp
    src/DocumentExport.cpp
    src/PdfPageAppender.cpp
)
target_include_directories(XournalPdf PUBLIC include PRIVATE src)
target_compile_options(XournalPdf PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalPdf PUBLIC XournalIo PRIVATE libqpdf dwrite windowscodecs fontsub ole32)
