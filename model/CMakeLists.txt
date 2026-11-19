# model: the document in memory - XML tree, pages, strokes and their ink, pictures, texts, backgrounds and rulings,
# references and snapshots, todo.txt lines. Plain C++ on base; no file contents are read or written here (io does) and
# no UI. Windows APIs used: path and string functions, CryptStringToBinary (base64), the ICU that ships with Windows.
add_library(XournalModel STATIC
    src/Xml.cpp
    src/Color.cpp
    src/XoppFormat.cpp
    src/StrokeShape.cpp
    src/Strokes.cpp
    src/ConvertedStrokes.cpp
    src/PageInk.cpp
    src/PageImages.cpp
    src/XoppPage.cpp
    src/ContentBottom.cpp
    src/Background.cpp
    src/BackgroundRuling.cpp
    src/XoppImage.cpp
    src/XoppText.cpp
    src/XoppClip.cpp
    src/DocumentNames.cpp
    src/XoppDocument.cpp
    src/ReferenceResolution.cpp
    src/ReferenceWriting.cpp
    src/Snapshot.cpp
    src/Attachments.cpp
    src/CopyPages.cpp
    src/TodoTxt.cpp
)
target_include_directories(XournalModel PUBLIC include PRIVATE src)
target_compile_options(XournalModel PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalModel PUBLIC XournalBase PRIVATE crypt32 icu)
