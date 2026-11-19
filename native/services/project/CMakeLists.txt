# project: the project folder for the explorer - the tree as it is on disk (ProjectTree), file operations
# (ProjectFiles: new folder, rename, recycle bin) and change notifications (FolderWatcher). Depends on base.
add_library(XournalProject STATIC src/ProjectTree.cpp src/ProjectFiles.cpp src/FolderWatcher.cpp)
target_include_directories(XournalProject PUBLIC include)
target_compile_options(XournalProject PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalProject PUBLIC XournalBase PRIVATE ole32 shell32)
