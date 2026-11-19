# app: XournalStudio.exe - the window and its feature classes (menu, toolbar, side pane, document session, page ink
# bridge, page underlay, messages, settings). Depends on io (and so model, base) and InkPages.
add_executable(XournalStudio WIN32
    src/main.cpp
    src/AppFolder.cpp
    src/Settings.cpp
    src/UiQueue.cpp
    src/MainMenu.cpp
    src/AppToolbar.cpp
    src/MainWindow.cpp
    src/SidePane.cpp
    src/ExplorerTree.cpp
    src/FolderListing.cpp
    src/Toast.cpp
    src/Dialogs.cpp
    src/PdfPages.cpp
    src/DocumentSession.cpp
    src/DocumentView.cpp
    src/InkConvert.cpp
    src/PageInkBridge.cpp
    src/PageUnderlay.cpp
    app.rc
)
target_compile_options(XournalStudio PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_options(XournalStudio PRIVATE /MANIFEST:NO) # the manifest is a resource (app.manifest)
# The pen cursors come from InkPages' demo (a dot per palette color x width step), included by app.rc.
target_include_directories(XournalStudio PRIVATE src ${INKPAGES_DIR}/demo)
target_link_libraries(XournalStudio PRIVATE XournalIo InkPages comctl32 shlwapi d2d1 dwmapi uxtheme)
