# settings: the native app's settings-native.json (Windows.Data.Json through C++/WinRT; never the C# app's
# settings.json). Depends on io (replacing writes) and so on model and base.
add_library(XournalSettings STATIC src/Settings.cpp)
target_include_directories(XournalSettings PUBLIC include)
target_compile_options(XournalSettings PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalSettings PUBLIC XournalIo PRIVATE windowsapp)
