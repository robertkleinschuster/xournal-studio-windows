# Third-party code of the git module, pinned release with SHA-256, built from source and linked statically:
#   libgit2 1.9.7 (GPL-2.0 with linking exception)   the repository: status, commit, fetch, rebase, push, checkout
#                                                     (the library LibGit2Sharp wraps for the C# app)
# HTTPS through Windows (WinHTTP, libgit2's default on Windows), hashes through Windows CNG; no SSH, no OpenSSL.
# Built once per configuration into the dependency cache (cmake/DependencyCache.cmake) as a project of its own, with
# the static CRT: libgit2's internal targets (e.g. "util") must not meet other projects' target names.
# zlib: the project's one zlib (top-level CMakeLists.txt), handed to libgit2's find_package(ZLIB).
xournal_cached_dependency(libgit2 VERSION 1.9.7
    URL https://github.com/libgit2/libgit2/archive/refs/tags/v1.9.7.tar.gz
    SHA256 1A4FBE7589E814777AE76B64734AD80F4ECAD22CD33A22682A2AAEA4AE5375E7
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF -DSTATIC_CRT=ON
        -DBUILD_TESTS=OFF -DBUILD_CLI=OFF -DBUILD_EXAMPLES=OFF
        -DUSE_SSH=OFF -DUSE_HTTPS=WinHTTP -DUSE_BUNDLED_ZLIB=OFF
        -DZLIB_INCLUDE_DIR=${zlib_BASE}/<CONFIG>/include
        -DZLIB_LIBRARY=${zlib_BASE}/<CONFIG>/lib/zlib.lib
    LICENSES COPYING)

# libgit2 for the git module and its tests: headers, the static library and the Windows libraries it needs.
add_library(xournal_libgit2 INTERFACE)
target_include_directories(xournal_libgit2 SYSTEM INTERFACE ${libgit2_PREFIX}/include)
target_link_libraries(xournal_libgit2 INTERFACE ${libgit2_PREFIX}/lib/git2.lib zlib
    winhttp rpcrt4 crypt32 ole32 secur32 advapi32)
