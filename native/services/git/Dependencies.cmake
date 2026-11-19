# Third-party code of the git module, pinned release with SHA-256, built from source and linked statically:
#   libgit2 1.9.7 (GPL-2.0 with linking exception)   the repository: status, commit, fetch, rebase, push, checkout
#                                                     (the library LibGit2Sharp wraps for the C# app)
# HTTPS through Windows (WinHTTP, libgit2's default on Windows), hashes through Windows CNG; no SSH, no OpenSSL.
# Built as a project of its own (ExternalProject), once per configuration with the static CRT: libgit2's internal
# targets (e.g. "util") must not meet other projects' target names (qpdf's tests have a "util" too).
# zlib: the project's one zlib target (top-level CMakeLists.txt), handed to libgit2's find_package(ZLIB).
include(ExternalProject)

set(XOURNAL_GIT2_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/libgit2)
ExternalProject_Add(libgit2_build
    URL https://github.com/libgit2/libgit2/archive/refs/tags/v1.9.7.tar.gz
    URL_HASH SHA256=1A4FBE7589E814777AE76B64734AD80F4ECAD22CD33A22682A2AAEA4AE5375E7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    DEPENDS zlib
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${XOURNAL_GIT2_PREFIX}/$<CONFIG>
        -DCMAKE_BUILD_TYPE=$<CONFIG>
        -DBUILD_SHARED_LIBS=OFF -DSTATIC_CRT=ON
        -DBUILD_TESTS=OFF -DBUILD_CLI=OFF -DBUILD_EXAMPLES=OFF
        -DUSE_SSH=OFF -DUSE_HTTPS=WinHTTP -DUSE_BUNDLED_ZLIB=OFF
        -DZLIB_INCLUDE_DIR=${zlib_SOURCE_DIR}
        -DZLIB_LIBRARY=$<TARGET_FILE:zlib>
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG>
    INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target install
    BUILD_BYPRODUCTS ${XOURNAL_GIT2_PREFIX}/Release/lib/git2.lib ${XOURNAL_GIT2_PREFIX}/Debug/lib/git2.lib)

# libgit2 for the git module and its tests: headers, the static library and the Windows libraries it needs.
file(MAKE_DIRECTORY ${XOURNAL_GIT2_PREFIX}/Release/include ${XOURNAL_GIT2_PREFIX}/Debug/include)
add_library(xournal_libgit2 INTERFACE)
add_dependencies(xournal_libgit2 libgit2_build)
target_include_directories(xournal_libgit2 SYSTEM INTERFACE ${XOURNAL_GIT2_PREFIX}/$<CONFIG>/include)
target_link_libraries(xournal_libgit2 INTERFACE ${XOURNAL_GIT2_PREFIX}/$<CONFIG>/lib/git2.lib zlib
    winhttp rpcrt4 crypt32 ole32 secur32 advapi32)
