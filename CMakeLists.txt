cmake_minimum_required(VERSION 3.25)
project(XournalStudio LANGUAGES C CXX RC)

# Xournal Studio for Windows, native: C++20 on Win32 and Windows APIs. Modules are static libraries whose link
# dependencies are the only allowed direction (see README.md):
#   base  <-  model  <-  io  <-  (later: services)  <-  app
# InkPages (the writing surface and its tools) is a git submodule in external/InkPages, used by the app only.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the app is a single exe without VC++ runtime DLLs next to it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(XOURNAL_COMPILE_OPTIONS /W4 /WX /permissive- /EHsc /utf-8)
set(XOURNAL_DEFINITIONS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

include(FetchContent)
# Third-party code is built once into a shared cache outside the build tree and reused by every worktree and CI run
# (cmake/DependencyCache.cmake); each module's Dependencies.cmake holds its pins.
include(cmake/DependencyCache.cmake)

# zlib: Windows has no public gzip API. The sources of the pinned release are compiled into a static library with the
# project's runtime by a small wrapper project (cmake/deps/zlib), because zlib's own CMake setup builds a DLL too and
# predates the policies this CMake needs.
xournal_cached_dependency(zlib VERSION 1.3.1
    URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    SHA256 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    SOURCE_WRAPPER ${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/zlib
    LICENSES LICENSE)
add_library(zlib INTERFACE)
target_include_directories(zlib SYSTEM INTERFACE ${zlib_PREFIX}/include)
target_link_libraries(zlib INTERFACE ${zlib_PREFIX}/lib/zlib.lib)

add_subdirectory(base)
add_subdirectory(model)
add_subdirectory(io)
add_subdirectory(pdf)
add_subdirectory(pdftext)
add_subdirectory(search)
add_subdirectory(import)
add_subdirectory(services)

# InkPages: only its library is built for the app (its demo, test tool and tests are built in its own repository).
set(INKPAGES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/InkPages)
add_subdirectory(${INKPAGES_DIR} external/InkPages EXCLUDE_FROM_ALL)
add_subdirectory(app)

# Unit tests: GoogleTest (pinned release, only for the tests, never in the app), run by CTest - as in InkPages.
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()
add_subdirectory(tests)
