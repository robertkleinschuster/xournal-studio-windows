# Third-party code of the pdf module, pinned releases with SHA-256, built from source and linked statically:
#   qpdf 12.4.2 (Apache-2.0)         PDF object model, page import, annotation flattening, writing
#   libjpeg-turbo 3.2.1 (BSD/IJG)    required by qpdf (DCT); the export never decodes or encodes JPEG with it
#   zlib                             the project's one zlib target (top-level CMakeLists.txt), shared with io
# qpdf's crypto: its own native implementation (no OpenSSL/GnuTLS) - needed to read and keep encrypted PDFs
# (e.g. lecture PDFs with a permissions password).
include(ExternalProject)

# --- libjpeg-turbo: its build refuses add_subdirectory, so ExternalProject (as its CMakeLists asks), once per
# configuration (static CRT /MT or /MTd like the rest of the build).
set(XOURNAL_JPEG_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo)
ExternalProject_Add(libjpeg_turbo_build
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.2.1/libjpeg-turbo-3.2.1.tar.gz
    URL_HASH SHA256=C27AD72A68DA151177929AA969AE773A2DA5AC2AF9CE946079314FDAC8CECE7F
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${XOURNAL_JPEG_PREFIX}/$<CONFIG>
        -DCMAKE_BUILD_TYPE=$<CONFIG>
        -DENABLE_SHARED=OFF -DENABLE_STATIC=ON
        -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF -DWITH_TESTS=OFF
        -DWITH_SIMD=OFF          # no NASM needed; qpdf only uses it to decode DCT on request
        -DWITH_CRT_DLL=OFF       # static CRT
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG>
    INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target install
    BUILD_BYPRODUCTS ${XOURNAL_JPEG_PREFIX}/Release/lib/jpeg-static.lib ${XOURNAL_JPEG_PREFIX}/Debug/lib/jpeg-static.lib)

# --- qpdf: only its static library is built (EXCLUDE_FROM_ALL; SYSTEM: its headers do not count for /W4 /WX).
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(USE_IMPLICIT_CRYPTO OFF CACHE BOOL "" FORCE)
set(REQUIRE_CRYPTO_NATIVE ON CACHE BOOL "" FORCE)
set(INSTALL_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)
set(INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
# qpdf finds zlib and libjpeg through these variables (libqpdf/CMakeLists.txt: set = no search).
set(ZLIB_H_PATH ${zlib_SOURCE_DIR} CACHE STRING "" FORCE)
set(ZLIB_LIB_PATH zlib CACHE STRING "" FORCE)
set(LIBJPEG_H_PATH ${XOURNAL_JPEG_PREFIX}/$<CONFIG>/include CACHE STRING "" FORCE)
set(LIBJPEG_LIB_PATH ${XOURNAL_JPEG_PREFIX}/$<CONFIG>/lib/jpeg-static.lib CACHE STRING "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON)  # no pkg-config lookups on Windows
FetchContent_Declare(qpdf
    URL https://github.com/qpdf/qpdf/releases/download/v12.4.2/qpdf-12.4.2.tar.gz
    URL_HASH SHA256=8A58AF5B6141319287C1883BEC8BD1BD545B7567B7FC5E6CE5D25A1C85F36397
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(qpdf)
add_dependencies(libqpdf_object libjpeg_turbo_build)
