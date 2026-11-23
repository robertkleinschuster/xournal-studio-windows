# io: files - the XML text (XmlLite), gzip (zlib), whole-file reads and replacing writes, .xopp load/save with
# attachments. Depends on model (and base).
add_library(XournalIo STATIC src/XmlText.cpp src/Gzip.cpp src/Files.cpp src/DocumentFile.cpp)
target_include_directories(XournalIo PUBLIC include)
target_compile_options(XournalIo PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalIo PUBLIC XournalModel PRIVATE ZLIB::ZLIB xmllite shlwapi)
