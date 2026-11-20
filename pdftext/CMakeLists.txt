# pdftext: the text of PDF pages with character boxes, and search with hit rectangles - PDFium (Windows has no PDF
# text API with positions: Windows.Data.Pdf has no text, the PDF IFilter no positions, OCR ~1 s per page; measured in
# D:\src\feature-probes\pdftext). A module of its own, not part of pdf: PDFium is a DLL (Google builds it only with
# Clang - no static MSVC build), delay-loaded, deployed next to the exe with its licenses; consumers that do not
# search (the export, the import) do not carry it. Depends on pdf (PageFrame: the shown-page coordinates) and base.
#
# PDFium 158.0.8125.0 (BSD-3-Clause; bundles FreeType, ICU, lcms, libjpeg-turbo, OpenJPEG, libpng, zlib, ... - their
# licenses come along in licenses/pdfium) from bblanchon/pdfium-binaries, release chromium/8125 (2026-10-26).
# Downloaded once into the dependency cache (cmake/DependencyCache.cmake).
xournal_cached_dependency(pdfium VERSION 158.0.8125.0
    URL https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F8125/pdfium-win-x64.tgz
    SHA256 9A964261760CFF52570AABF038AB346D3A5034DD8B30D0D5FE8861235D356B8B
    DOWNLOAD_ONLY) # binaries and headers only
set(XOURNAL_PDFIUM_DIR ${pdfium_PREFIX} CACHE INTERNAL "")

add_library(pdfium SHARED IMPORTED GLOBAL)
set_target_properties(pdfium PROPERTIES
    IMPORTED_LOCATION ${XOURNAL_PDFIUM_DIR}/bin/pdfium.dll
    IMPORTED_IMPLIB ${XOURNAL_PDFIUM_DIR}/lib/pdfium.dll.lib)
target_include_directories(pdfium SYSTEM INTERFACE ${XOURNAL_PDFIUM_DIR}/include)

add_library(XournalPdfText STATIC src/Pdfium.cpp src/PdfText.cpp)
target_include_directories(XournalPdfText PUBLIC include PRIVATE src)
target_compile_options(XournalPdfText PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalPdfText PUBLIC XournalPdf PRIVATE pdfium delayimp)
# pdfium.dll is loaded on first use (Pdfium.cpp), so an exe starts without it and a missing DLL only disables search.
target_link_options(XournalPdfText INTERFACE /DELAYLOAD:pdfium.dll)

# Puts pdfium.dll and its licenses (licenses/pdfium) next to an exe that links XournalPdfText.
function(xournal_deploy_pdfium target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XOURNAL_PDFIUM_DIR}/bin/pdfium.dll $<TARGET_FILE_DIR:${target}>
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${XOURNAL_PDFIUM_DIR}/licenses $<TARGET_FILE_DIR:${target}>/licenses/pdfium
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${XOURNAL_PDFIUM_DIR}/LICENSE $<TARGET_FILE_DIR:${target}>/licenses/pdfium/LICENSE
        VERBATIM)
endfunction()
