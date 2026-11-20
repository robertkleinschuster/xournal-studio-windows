# import: Datei -> PDF mit iPad-Anmerkungen importieren. Splits a PDF annotated on the iPad into the PDF without that
# markup and the .xopp elements for it: PencilKit handwriting as strokes, text boxes as texts, everything else that shows
# something as pictures (rendered by the pdf module with Windows.Data.Pdf). Reads annotations with qpdf. Depends on pdf
# (and so io, model, base); nothing depends on import but the app.
#
# Formats without a Windows API:
#   - protobuf (PencilKit drawings): protozero 1.8.2 (BSD-2-Clause, header-only, Mapbox) - a small established decoder;
#     the full Google protobuf library would be far beyond what reading these few fields needs.
#   - binary plist (Apple's NSKeyedArchiver archive): src/BinaryPlist - no small permissive C++ library fits (libplist
#     is LGPL-2.1, others pull in Boost); a reader of exactly the bplist00 format, tested.
xournal_cached_dependency(protozero VERSION 1.8.2
    URL https://github.com/mapbox/protozero/archive/refs/tags/v1.8.2.zip
    SHA256 401FF87C64532AC60AA05E9EB4CC129D47A00D963AA67DD606DA3F3DDF5C16F3
    DOWNLOAD_ONLY  # headers only (dependency cache, cmake/DependencyCache.cmake): its CMake builds tests and tools
    LICENSES LICENSE.md LICENSE.from_folly)
add_library(protozero INTERFACE)
target_include_directories(protozero SYSTEM INTERFACE ${protozero_PREFIX}/include)

add_library(XournalImport STATIC
    src/BinaryPlist.cpp
    src/AppleMarkup.cpp
    src/PencilKitDrawing.cpp
    src/AppearanceText.cpp
    src/PdfMarkupImport.cpp
)
target_include_directories(XournalImport PUBLIC include PRIVATE src)
target_compile_options(XournalImport PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalImport PUBLIC XournalPdf PRIVATE libqpdf protozero crypt32)
