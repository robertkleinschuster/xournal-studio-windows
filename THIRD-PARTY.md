# Third-party code in the native app

Libraries come from vcpkg (manifest `vcpkg.json`, pinned by its baseline; built for `x64-windows-static`, linked statically);
PDFium is a prebuilt release fetched with CMake's FetchContent (URL + SHA-256). Their license texts are staged in `licenses\`. Licenses of what ships with the app must go into its installer (a `licenses` folder next to the exe).

| Component | Version | License | Where | Ships |
|---|---|---|---|---|
| zlib | 1.3.1 | zlib | `vcpkg.json` (gzip in io; also used by qpdf and libgit2) | static |
| qpdf | 12.4.2 | Apache-2.0 (needs its NOTICE/license text) | `vcpkg.json` | static |
| libjpeg-turbo | 3.2.1 | IJG, BSD-3-Clause, zlib | `vcpkg.json` (qpdf needs it) | static |
| protozero | 1.8.2 | BSD-2-Clause | `vcpkg.json` (header-only) | static |
| PDFium | 158.0.8125.0 (bblanchon/pdfium-binaries, chromium/8125) | BSD-3-Clause; bundles abseil, FreeType, HarfBuzz, highway, ICU, lcms, libjpeg-turbo, OpenJPEG, libpng, zlib and others under their licenses | `pdftext/CMakeLists.txt` | `pdfium.dll` (delay-loaded); `xournal_deploy_pdfium` copies its `LICENSE` and `licenses/` to `licenses/pdfium` next to the exe |
| libgit2 | 1.9.7 | GPL-2.0 with the linking exception; xdiff (LGPL-2.1+) and SHA-1 collision detection (MIT) are covered by its `COPYING`; the vcpkg port links the separate ports http-parser (MIT) and PCRE (BSD-3-Clause), whose copyright texts are staged too | `vcpkg.json` (HTTPS through WinHTTP, the shared zlib) | static; `licenses/libgit2`, `licenses/pcre`, `licenses/http-parser` |
| InkPages | submodule | the project's own | `external/InkPages` | static |
| win32-darkmodelib | `e8ec95b` | MPL-2.0 and MIT | `app/Darkmodelib.cmake` | static; [upstream source](https://github.com/ozone10/win32-darkmodelib/tree/e8ec95ba631462f01e238e63f7d06c1e6829a7a1) and [status text change](https://github.com/robertkleinschuster/xournal-studio-windows/blob/main/app/Darkmodelib.cmake) |
| GoogleTest | 1.15.2 | BSD-3-Clause | `CMakeLists.txt` | tests only, not shipped |
