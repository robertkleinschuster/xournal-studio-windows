# zlib for the dependency cache: the sources of the pinned zlib release (XOURNAL_DEP_SOURCE_DIR) compiled into one
# static library with the static CRT and installed with its headers. zlib's own CMake setup builds a DLL too and
# predates the policies the current CMake needs. The files are those the app, qpdf and libgit2 use (inflate/deflate).
cmake_minimum_required(VERSION 3.25)
project(xournal_zlib C)
set(src ${XOURNAL_DEP_SOURCE_DIR})
add_library(zlib STATIC
    ${src}/adler32.c ${src}/crc32.c ${src}/deflate.c ${src}/inffast.c ${src}/inflate.c ${src}/inftrees.c
    ${src}/trees.c ${src}/zutil.c)
target_compile_definitions(zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
install(TARGETS zlib ARCHIVE DESTINATION lib)
install(FILES ${src}/zlib.h ${src}/zconf.h DESTINATION include)
