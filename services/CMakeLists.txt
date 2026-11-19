# services: the app's non-UI services, one static library per module (see README.md). Their link dependencies are the
# only allowed direction; none of them depends on the app or on another service except where listed:
#   project   (base)                   explorer tree, file operations, folder watcher
#   git       (base, libgit2)          status, sync, discard, Credential Manager sign-in
#   tasks     (io)                     todo.txt line edits
#   recovery  (io, project)            recovery copies and the autosave queue
#   settings  (io, Windows.Data.Json)  the native app's settings-native.json
add_subdirectory(project)
add_subdirectory(git)
add_subdirectory(tasks)
add_subdirectory(recovery)
add_subdirectory(settings)
