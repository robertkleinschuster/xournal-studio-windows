# base: errors with HRESULTs, the log, text helpers. Depends on nothing but Windows.
add_library(XournalBase STATIC src/Error.cpp src/Log.cpp src/Text.cpp)
target_include_directories(XournalBase PUBLIC include)
target_compile_options(XournalBase PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_compile_definitions(XournalBase PUBLIC ${XOURNAL_DEFINITIONS})
