# search: Strg+F over a whole document - the PDF's text (pdftext), the file's texts (model; measured on the export's
# DirectWrite layout) and handwriting recognised by Windows (InkAnalyzer, cached by the strokes, memory only). No UI.
# Depends on pdftext (and so pdf, io, model, base).
add_library(XournalSearch STATIC src/TextMatch.cpp src/Handwriting.cpp src/Search.cpp)
target_include_directories(XournalSearch PUBLIC include PRIVATE src)
target_compile_options(XournalSearch PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalSearch PUBLIC XournalPdfText PRIVATE dwrite windowsapp)
