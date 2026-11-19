# recovery: recovery copies of documents with unsaved changes (Recovery: copy + info JSON with Windows.Data.Json, the
# writing process' id and start time; recycling with project's ProjectFiles) and the ordered write/delete queue the app
# drives (Autosave). Depends on io (document writes) and project (recycle bin).
add_library(XournalRecovery STATIC src/Recovery.cpp src/Autosave.cpp)
target_include_directories(XournalRecovery PUBLIC include)
target_compile_options(XournalRecovery PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalRecovery PUBLIC XournalIo PRIVATE XournalProject windowsapp ole32)
