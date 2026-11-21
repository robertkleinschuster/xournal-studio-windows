set(darkmodelib_revision e8ec95ba631462f01e238e63f7d06c1e6829a7a1)
FetchContent_Declare(darkmodelib
    URL https://codeload.github.com/ozone10/win32-darkmodelib/zip/${darkmodelib_revision}
    URL_HASH SHA256=82a6c1ea7250d4671c7c814e1d2f47e97c4bf2cf825b9d6fb4637a76a835c303
)
FetchContent_MakeAvailable(darkmodelib)

# Keep the native status text and fix the library's dark-mode painter.
set(status_painter "${darkmodelib_SOURCE_DIR}/src/DmlibSubclassControl.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${status_painter}")
file(READ "${status_painter}" control_source)
string(REPLACE
    "str.c_str(), -1, &rcPart, DT_SINGLELINE | DT_VCENTER | DT_LEFT);"
    "str.c_str(), -1, &rcPart, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);"
    patched_control_source "${control_source}")
if(patched_control_source STREQUAL control_source)
    message(FATAL_ERROR "Recheck the darkmodelib status text patch after updating the dependency")
endif()
set(patched_control_file "${CMAKE_CURRENT_BINARY_DIR}/DmlibSubclassControl.cpp")
file(CONFIGURE OUTPUT "${patched_control_file}" CONTENT "${patched_control_source}" @ONLY)

add_library(XournalDarkmodelib STATIC
    ${darkmodelib_SOURCE_DIR}/src/Darkmodelib.cpp
    ${darkmodelib_SOURCE_DIR}/src/DmlibColor.cpp
    ${darkmodelib_SOURCE_DIR}/src/DmlibDpi.cpp
    ${darkmodelib_SOURCE_DIR}/src/DmlibHook.cpp
    ${darkmodelib_SOURCE_DIR}/src/DmlibPaintHelper.cpp
    ${darkmodelib_SOURCE_DIR}/src/DmlibSubclass.cpp
    "${patched_control_file}"
    ${darkmodelib_SOURCE_DIR}/src/DmlibSubclassWindow.cpp
    ${darkmodelib_SOURCE_DIR}/src/DmlibWinApi.cpp
)
target_include_directories(XournalDarkmodelib SYSTEM PUBLIC ${darkmodelib_SOURCE_DIR}/include)
target_include_directories(XournalDarkmodelib PRIVATE ${darkmodelib_SOURCE_DIR}/src)
target_compile_features(XournalDarkmodelib PRIVATE cxx_std_20)
target_compile_definitions(XournalDarkmodelib PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    _DARKMODELIB_NO_INI_CONFIG _DARKMODELIB_CUSTOM_MEM=0x002
)
target_compile_options(XournalDarkmodelib PRIVATE /utf-8)
target_link_libraries(XournalDarkmodelib PRIVATE comctl32 comdlg32 dwmapi gdi32 shlwapi uxtheme)

file(COPY
    ${darkmodelib_SOURCE_DIR}/LICENSE.md
    ${darkmodelib_SOURCE_DIR}/LICENSE-MIT.md
    ${darkmodelib_SOURCE_DIR}/docs/LICENSE-PolyHook_2_0.md
    ${darkmodelib_SOURCE_DIR}/docs/LICENSE-UAHMenuBar.md
    ${darkmodelib_SOURCE_DIR}/docs/LICENSE-win32-darkmode.md
    DESTINATION ${CMAKE_BINARY_DIR}/licenses/darkmodelib
)
