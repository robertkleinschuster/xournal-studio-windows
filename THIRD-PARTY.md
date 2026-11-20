# Third-party code in the native app

Fetched at pinned releases (URL + SHA-256 in the CMake files named below), built from source once into the shared
dependency cache (`cmake/DependencyCache.cmake`) and linked statically unless noted. Licenses of what ships with the app must go into its installer (a `licenses` folder next to the exe).

| Component | Version | License | Where | Ships |
|---|---|---|---|---|
| zlib | 1.3.1 | zlib | `CMakeLists.txt` (gzip in io; also used by qpdf) | static |
| qpdf | 12.4.2 | Apache-2.0 (needs its NOTICE/license text) | `pdf/Dependencies.cmake` | static |
| libjpeg-turbo | 3.2.1 | IJG, BSD-3-Clause, zlib | `pdf/Dependencies.cmake` (qpdf needs it) | static |
| protozero | 1.8.2 | BSD-2-Clause | `import/CMakeLists.txt` (header-only) | static |
| PDFium | 158.0.8125.0 (bblanchon/pdfium-binaries, chromium/8125) | BSD-3-Clause; bundles abseil, FreeType, HarfBuzz, highway, ICU, lcms, libjpeg-turbo, OpenJPEG, libpng, zlib and others under their licenses | `pdftext/CMakeLists.txt` | `pdfium.dll` (delay-loaded); `xournal_deploy_pdfium` copies its `LICENSE` and `licenses/` to `licenses/pdfium` next to the exe |
| libgit2 | 1.9.7 | GPL-2.0 with the linking exception; its bundled llhttp (MIT), PCRE (BSD-3-Clause), xdiff (LGPL-2.1+) and SHA-1 collision detection (MIT) are covered by its `COPYING` | `services/git/Dependencies.cmake` (a project of its own; HTTPS through WinHTTP, the shared zlib) | static; `install.ps1` copies `COPYING` to `licenses/libgit2` |
| InkPages | submodule | the project's own | `external/InkPages` | static |
| GoogleTest | 1.15.2 | BSD-3-Clause | `CMakeLists.txt` | tests only, not shipped |
