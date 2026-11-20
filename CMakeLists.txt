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

# Compiler cache for the project's own code (build.ps1 turns it on): ccache (pinned release, downloaded into the
# dependency cache) as the compiler launcher - launchers work with the Ninja generators only. CCACHE_BASEDIR makes the
# paths relative to the source root, so every worktree hits the same cache (ccache's default directory, or CCACHE_DIR).
# Debug information embedded in the objects (/Z7): ccache cannot cache /Zi's shared PDB.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
option(XOURNAL_CCACHE "Compile the project's code through ccache (Ninja generators only)" OFF)
if(XOURNAL_CCACHE)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja")
        message(FATAL_ERROR "XOURNAL_CCACHE needs a Ninja generator (compiler launchers), not ${CMAKE_GENERATOR}")
    endif()
    xournal_cached_dependency(ccache VERSION 4.14.2
        URL https://github.com/ccache/ccache/releases/download/v4.14.2/ccache-4.14.2-windows-x86_64.zip
        SHA256 6993ce645e53192cec592ee23186200b574ac4c59097ecde8d9cfd681b47ae84
        DOWNLOAD_ONLY)
    # ccache settings for sharing between worktrees and CI machines: paths relative to the source root, the build
    # folder not hashed, the compiler identified by its content (not its file time), and the /showIncludes prefix of
    # the installed MSVC language (CMake detects it; a German MSVC writes "Hinweis: Einlesen der Datei:").
    set(XOURNAL_CCACHE_LAUNCHER ${CMAKE_COMMAND} -E env CCACHE_BASEDIR=${CMAKE_SOURCE_DIR} CCACHE_NOHASHDIR=1
        CCACHE_COMPILERCHECK=content "CCACHE_MSVC_DEP_PREFIX=${CMAKE_CL_SHOWINCLUDES_PREFIX}" ${ccache_PREFIX}/ccache.exe)
    set(CMAKE_C_COMPILER_LAUNCHER ${XOURNAL_CCACHE_LAUNCHER})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${XOURNAL_CCACHE_LAUNCHER})
    set(XOURNAL_CCACHE_EXE ${ccache_PREFIX}/ccache.exe CACHE INTERNAL "")
    message(STATUS "ccache: ${XOURNAL_CCACHE_EXE}")
endif()

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
