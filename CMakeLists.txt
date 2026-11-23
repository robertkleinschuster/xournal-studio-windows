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

# Third-party libraries come from vcpkg in manifest mode (vcpkg.json, pinned by its builtin-baseline), built for the
# triplet x64-windows-static (static libraries, static CRT) by the toolchain file CMakePresets.json sets, and reused
# from vcpkg's binary cache. PDFium (prebuilt binaries, no vcpkg port) is fetched in pdftext/CMakeLists.txt.
find_package(ZLIB REQUIRED)
find_package(qpdf CONFIG REQUIRED)
find_package(libgit2 CONFIG REQUIRED)
find_path(PROTOZERO_INCLUDE_DIR protozero/pbf_reader.hpp REQUIRED) # header-only, vcpkg ships no CMake package

# qpdf's CMake package names its dependencies without a path (zlib, jpeg, and the debug names zlibd, jpeg): the linker
# finds them in vcpkg's library folder of the configuration being built.
link_directories("${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>lib")

# The license texts of the libraries linked statically (vcpkg: share/<port>/copyright) go to <build>/licenses/<port>;
# install.ps1 stages them next to the exe.
file(GLOB xournal_vcpkg_licenses "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*/copyright")
if(NOT xournal_vcpkg_licenses)
    message(FATAL_ERROR "no vcpkg copyright files in ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
endif()
foreach(license IN LISTS xournal_vcpkg_licenses)
    get_filename_component(port "${license}" DIRECTORY)
    get_filename_component(port "${port}" NAME)
    file(COPY "${license}" DESTINATION "${CMAKE_BINARY_DIR}/licenses/${port}")
endforeach()

# Compiler cache for the project's own code (build.ps1 turns it on): the installed ccache (winget install
# Ccache.Ccache) as the compiler launcher - launchers work with the Ninja generators only. CCACHE_BASEDIR makes the
# paths relative to the source root, so every worktree hits the same cache (ccache's default directory, or CCACHE_DIR).
# Debug information embedded in the objects (/Z7): ccache cannot cache /Zi's shared PDB.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
option(XOURNAL_CCACHE "Compile the project's code through ccache (Ninja generators only)" OFF)
if(XOURNAL_CCACHE)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja")
        message(FATAL_ERROR "XOURNAL_CCACHE needs a Ninja generator (compiler launchers), not ${CMAKE_GENERATOR}")
    endif()
    find_program(XOURNAL_CCACHE_EXE ccache REQUIRED) # winget install Ccache.Ccache
    # ccache settings for sharing between worktrees and CI machines: paths relative to the source root, the build
    # folder not hashed, the compiler identified by its content (not its file time), and the /showIncludes prefix of
    # the installed MSVC language (CMake detects it; a German MSVC writes "Hinweis: Einlesen der Datei:").
    set(XOURNAL_CCACHE_LAUNCHER ${CMAKE_COMMAND} -E env CCACHE_BASEDIR=${CMAKE_SOURCE_DIR} CCACHE_NOHASHDIR=1
        CCACHE_COMPILERCHECK=content "CCACHE_MSVC_DEP_PREFIX=${CMAKE_CL_SHOWINCLUDES_PREFIX}" ${XOURNAL_CCACHE_EXE})
    set(CMAKE_C_COMPILER_LAUNCHER ${XOURNAL_CCACHE_LAUNCHER})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${XOURNAL_CCACHE_LAUNCHER})
    message(STATUS "ccache: ${XOURNAL_CCACHE_EXE}")
endif()

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
