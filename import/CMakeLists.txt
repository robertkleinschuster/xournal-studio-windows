# import: Datei -> PDF mit iPad-Anmerkungen importieren. Splits a PDF annotated on the iPad into the PDF without that
# markup and the .xopp elements for it: PencilKit handwriting as strokes, text boxes as texts, everything else that shows
# something as pictures (rendered by the pdf module with Windows.Data.Pdf). Reads annotations with qpdf. Depends on pdf
# (and so io, model, base); nothing depends on import but the app.
#
# Formats without a Windows API:
#   - protobuf (PencilKit drawings): protozero 1.8.2 (BSD-2-Clause, header-only, Mapbox; vcpkg) - a small established decoder;
#     the full Google protobuf library would be far beyond what reading these few fields needs.
#   - binary plist (Apple's NSKeyedArchiver archive): src/BinaryPlist - no small permissive C++ library fits (libplist
#     is LGPL-2.1, others pull in Boost); a reader of exactly the bplist00 format, tested.

add_library(protozero INTERFACE)
target_include_directories(protozero SYSTEM INTERFACE ${PROTOZERO_INCLUDE_DIR})

add_library(XournalImport STATIC
    src/BinaryPlist.cpp
    src/BinaryPlistWriter.cpp
    src/AppleMarkup.cpp
    src/PencilKitDrawing.cpp
    src/AppearanceText.cpp
    src/PdfMarkupImport.cpp
    src/IpadExport.cpp
    src/IpadElements.cpp
)
target_include_directories(XournalImport PUBLIC include PRIVATE src)
target_compile_options(XournalImport PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalImport PUBLIC XournalPdf PRIVATE qpdf::libqpdf protozero crypt32)
