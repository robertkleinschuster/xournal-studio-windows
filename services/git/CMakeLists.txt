# git: the project folder's git repository - status for the explorer, sync (commit all, fetch, rebase/fast-forward,
# push), discarding one file - on libgit2, and the sign-in Git for Windows keeps in the Credential Manager.
# Depends on base; libgit2 is private (no libgit2 type in the public headers).
include(Dependencies.cmake)

add_library(XournalGit STATIC src/LibGit2.cpp src/GitSync.cpp src/GitCredentials.cpp)
target_include_directories(XournalGit PUBLIC include PRIVATE src)
target_compile_options(XournalGit PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalGit PUBLIC XournalBase PRIVATE xournal_libgit2)
