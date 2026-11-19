# tasks: the project's todo.txt changed line by line (TaskFile); parsing a line is the model's TodoTxt. Depends on io
# (replacing writes) and so on model and base.
add_library(XournalTasks STATIC src/TaskFile.cpp)
target_include_directories(XournalTasks PUBLIC include)
target_compile_options(XournalTasks PRIVATE ${XOURNAL_COMPILE_OPTIONS})
target_link_libraries(XournalTasks PUBLIC XournalIo)
