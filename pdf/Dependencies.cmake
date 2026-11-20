# Third-party code of the pdf module, pinned releases with SHA-256, built from source and linked statically:
#   qpdf 12.4.2 (Apache-2.0)         PDF object model, page import, annotation flattening, writing
#   libjpeg-turbo 3.2.1 (BSD/IJG)    required by qpdf (DCT); the export never decodes or encodes JPEG with it
#   zlib                             the project's one zlib target (top-level CMakeLists.txt), shared with io
# qpdf's crypto: its own native implementation (no OpenSSL/GnuTLS) - needed to read and keep encrypted PDFs
# (e.g. lecture PDFs with a permissions password).
# Both are built once into the dependency cache (cmake/DependencyCache.cmake), each configuration with the static CRT.

# --- libjpeg-turbo: built as a project of its own (its build refuses add_subdirectory).
xournal_cached_dependency(libjpeg-turbo VERSION 3.2.1
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.1/libjpeg-turbo-3.2.1.tar.gz
    SHA256 C27AD72A68DA151177929AA969AE773A2DA5AC2AF9CE946079314FDAC8CECE7F
    CMAKE_ARGS
        -DENABLE_SHARED=OFF -DENABLE_STATIC=ON
        -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF
        -DWITH_SIMD=OFF          # no NASM needed; qpdf only uses it to decode DCT on request
        -DWITH_CRT_DLL=OFF       # static CRT
    LICENSES LICENSE.md)

# --- qpdf: only its static library, with its native crypto; zlib and libjpeg from the cache through the variables
# qpdf reads (libqpdf/CMakeLists.txt: set = no search). Exposed as the target libqpdf (as qpdf's own build names it).
xournal_cached_dependency(qpdf VERSION 12.4.2
    URL https://github.com/qpdf/qpdf/releases/download/v12.4.2/qpdf-12.4.2.tar.gz
    SHA256 8A58AF5B6141319287C1883BEC8BD1BD545B7567B7FC5E6CE5D25A1C85F36397
    BUILD_TARGET libqpdf INSTALL_COMPONENT dev  # the library and its headers, not the qpdf tool, examples or tests
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON -DBUILD_DOC=OFF
        -DUSE_IMPLICIT_CRYPTO=OFF -DREQUIRE_CRYPTO_NATIVE=ON
        -DINSTALL_PKGCONFIG=OFF -DINSTALL_CMAKE_PACKAGE=OFF -DINSTALL_EXAMPLES=OFF
        -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON  # no pkg-config lookups on Windows
        -DZLIB_H_PATH=${zlib_BASE}/<CONFIG>/include
        -DZLIB_LIB_PATH=${zlib_BASE}/<CONFIG>/lib/zlib.lib
        -DLIBJPEG_H_PATH=${libjpeg-turbo_BASE}/<CONFIG>/include
        -DLIBJPEG_LIB_PATH=${libjpeg-turbo_BASE}/<CONFIG>/lib/jpeg-static.lib
    LICENSES LICENSE.txt NOTICE.md)
add_library(libqpdf INTERFACE)
target_include_directories(libqpdf SYSTEM INTERFACE ${qpdf_PREFIX}/include)
target_link_libraries(libqpdf INTERFACE ${qpdf_PREFIX}/lib/qpdf.lib ${libjpeg-turbo_PREFIX}/lib/jpeg-static.lib zlib)
