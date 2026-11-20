# Dependency cache: each third-party dependency is built once per (name, version, recipe, configuration, toolchain)
# into a shared install prefix outside the worktrees and reused by every build tree - fresh worktrees, install.ps1's
# worktree and CI (actions/cache of the same folder) skip rebuilding qpdf, libjpeg-turbo, libgit2 and zlib.
#
# Root: the CMake variable XOURNAL_DEPS_CACHE, else the environment variable XOURNAL_DEPS_CACHE, else
# %LOCALAPPDATA%\XournalStudioBuildCache. Layout:
#   <root>/downloads/<sha256 prefix>-<file>        downloaded archives (checked against their pinned SHA-256)
#   <root>/<name>-<version>-<key>/<config>/        install prefix of one configuration (include, lib, licenses)
#   <root>/<name>-<version>-<key>/<config>-*.log   the logs of its configure, build and install
#   <root>/<name>-<version>-<key>/files/           a download-only dependency (headers, binaries)
# <key> = SHA-256 (first 16 hex digits) of the recipe (URL, hash, CMake arguments - which name the prefixes of the
# dependencies it builds on, so their keys too), the files of a wrapper project, this file, and the toolchain
# (compiler id and versions, generator, platform, toolset, Windows SDK). A changed pin, recipe or compiler gives a new
# folder; a stale build is never reused. Old folders are not deleted automatically (delete the root to clean up); each
# configure touches <root>/<name>-<version>-<key>/last-used (CI saves only the folders its run used).
#
# Concurrent builds: one configuration of one dependency is built under a file lock (file(LOCK)) into <config>.tmp
# and renamed to <config> when installed completely; a folder that exists is complete. Builds run at configure time
# with the same generator, platform and Windows SDK as the main project and the static CRT. Any failure stops the
# configure with its reason and the log's tail.
#
# XOURNAL_DEPS_CONFIGS: the configurations built (default Release;Debug; build.ps1 passes its -Config). A build of a
# configuration that is not in it fails to link with the missing library's path.

set(XOURNAL_DEPS_CONFIGS "Release;Debug" CACHE STRING "Configurations of the cached third-party dependencies")
if(NOT XOURNAL_DEPS_CACHE)
    if(DEFINED ENV{XOURNAL_DEPS_CACHE} AND NOT "$ENV{XOURNAL_DEPS_CACHE}" STREQUAL "")
        set(XOURNAL_DEPS_CACHE "$ENV{XOURNAL_DEPS_CACHE}")
    elseif(DEFINED ENV{LOCALAPPDATA})
        set(XOURNAL_DEPS_CACHE "$ENV{LOCALAPPDATA}/XournalStudioBuildCache")
    else()
        message(FATAL_ERROR "Dependency cache: no root - set XOURNAL_DEPS_CACHE (CMake or environment variable)")
    endif()
endif()
file(TO_CMAKE_PATH "${XOURNAL_DEPS_CACHE}" XOURNAL_DEPS_CACHE)
set(XOURNAL_DEPS_CACHE "${XOURNAL_DEPS_CACHE}" CACHE PATH "Root of the shared third-party dependency cache" FORCE)
message(STATUS "Dependency cache: ${XOURNAL_DEPS_CACHE} (configurations: ${XOURNAL_DEPS_CONFIGS})")

file(SHA256 "${CMAKE_CURRENT_LIST_FILE}" _xournal_deps_cache_logic)
set(_xournal_deps_toolchain
    "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    "${CMAKE_GENERATOR}|${CMAKE_GENERATOR_PLATFORM}|${CMAKE_GENERATOR_TOOLSET}|${CMAKE_SYSTEM_VERSION}"
    "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}|${_xournal_deps_cache_logic}")

# Downloads <url> once into <root>/downloads, verified against <sha256>; sets <out_var> to the file.
function(_xournal_deps_download name url sha256 out_var)
    get_filename_component(file_name "${url}" NAME)
    string(SUBSTRING "${sha256}" 0 16 short)
    set(file "${XOURNAL_DEPS_CACHE}/downloads/${short}-${file_name}")
    if(EXISTS "${file}")
        file(SHA256 "${file}" actual)
        if(NOT actual STREQUAL sha256)
            message(FATAL_ERROR "Dependency cache: ${file} has SHA-256 ${actual}, the pin says ${sha256} - delete the file")
        endif()
    else()
        string(RANDOM LENGTH 8 suffix)
        set(part "${file}.${suffix}.part")
        message(STATUS "Dependency cache: downloading ${name} ${url}")
        file(DOWNLOAD "${url}" "${part}" EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON STATUS status)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${part}")
            message(FATAL_ERROR "Dependency cache: download of ${name} failed: ${status} (${url})")
        endif()
        file(RENAME "${part}" "${file}") # atomic; a concurrent download of the same file writes the same bytes
    endif()
    set(${out_var} "${file}" PARENT_SCOPE)
endfunction()

# Extracts <archive> into <dir>; sets <out_var> to the source root (the archive's single top folder, if it has one).
function(_xournal_deps_extract archive dir out_var)
    file(REMOVE_RECURSE "${dir}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${dir}")
    file(GLOB entries LIST_DIRECTORIES true "${dir}/*")
    list(LENGTH entries count)
    if(count EQUAL 1 AND IS_DIRECTORY "${entries}")
        set(${out_var} "${entries}" PARENT_SCOPE)
    else()
        set(${out_var} "${dir}" PARENT_SCOPE)
    endif()
endfunction()

function(_xournal_deps_run step log)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_FILE "${log}" ERROR_FILE "${log}")
    if(NOT result EQUAL 0)
        file(READ "${log}" text)
        string(LENGTH "${text}" length)
        if(length GREATER 4000)
            math(EXPR start "${length} - 4000")
            string(SUBSTRING "${text}" ${start} -1 text)
        endif()
        message(FATAL_ERROR "Dependency cache: ${step} failed (${result}). Log ${log}, its end:\n${text}")
    endif()
endfunction()

# Builds one configuration under the lock unless its folder exists.
function(_xournal_deps_build_config name base config archive)
    cmake_parse_arguments(PARSE_ARGV 4 arg "" "SOURCE_WRAPPER;BUILD_TARGET;INSTALL_COMPONENT" "CMAKE_ARGS;LICENSES")
    set(prefix "${base}/${config}")
    if(EXISTS "${prefix}")
        return()
    endif()
    file(LOCK "${base}/${config}.lock" GUARD FUNCTION TIMEOUT 7200 RESULT_VARIABLE lock_result)
    if(NOT lock_result STREQUAL "0")
        message(FATAL_ERROR "Dependency cache: lock ${base}/${config}.lock: ${lock_result}")
    endif()
    if(EXISTS "${prefix}") # built by a concurrent configure while this one waited
        message(STATUS "Dependency cache: ${name} (${config}) built meanwhile by another build")
        return()
    endif()
    message(STATUS "Dependency cache: building ${name} (${config}) into ${prefix}")
    string(TIMESTAMP started "%s")
    set(work "${base}/${config}.tmp")
    file(REMOVE_RECURSE "${work}")
    _xournal_deps_extract("${archive}" "${work}/src" source)
    set(project_dir "${source}")
    if(arg_SOURCE_WRAPPER)
        set(project_dir "${arg_SOURCE_WRAPPER}")
    endif()
    string(REPLACE "<CONFIG>" "${config}" cmake_args "${arg_CMAKE_ARGS}")
    set(runtime MultiThreaded)
    if(config STREQUAL "Debug")
        set(runtime MultiThreadedDebug)
    endif()
    set(generator -G "${CMAKE_GENERATOR}")
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND generator -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND generator -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    set(log "${base}/${config}")
    _xournal_deps_run("configure of ${name} (${config})" "${log}-configure.log"
        "${CMAKE_COMMAND}" -S "${project_dir}" -B "${work}/build" ${generator}
        "-DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}"
        "-DCMAKE_BUILD_TYPE=${config}"
        "-DCMAKE_INSTALL_PREFIX=${work}/install"
        "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW"
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=${runtime}"
        "-DXOURNAL_DEP_SOURCE_DIR=${source}"
        ${cmake_args})
    set(target "")
    if(arg_BUILD_TARGET)
        set(target --target ${arg_BUILD_TARGET})
    endif()
    set(component "")
    if(arg_INSTALL_COMPONENT)
        set(component --component ${arg_INSTALL_COMPONENT})
    endif()
    _xournal_deps_run("build of ${name} (${config})" "${log}-build.log"
        "${CMAKE_COMMAND}" --build "${work}/build" --config ${config} --parallel ${target})
    _xournal_deps_run("install of ${name} (${config})" "${log}-install.log"
        "${CMAKE_COMMAND}" --install "${work}/build" --config ${config} ${component})
    foreach(license IN LISTS arg_LICENSES)
        if(NOT EXISTS "${source}/${license}")
            message(FATAL_ERROR "Dependency cache: license text ${license} of ${name} missing in ${source}")
        endif()
        file(COPY "${source}/${license}" DESTINATION "${work}/install/licenses/${name}")
    endforeach()
    file(RENAME "${work}/install" "${prefix}")
    file(REMOVE_RECURSE "${work}")
    string(TIMESTAMP finished "%s")
    math(EXPR seconds "${finished} - ${started}")
    message(STATUS "Dependency cache: ${name} (${config}) built in ${seconds} s")
endfunction()

# xournal_cached_dependency(<name> VERSION <v> URL <url> SHA256 <hash>
#     [BUILD_TARGET <target>] [INSTALL_COMPONENT <component>]  only that target / install component (default: all)
#     [SOURCE_WRAPPER <dir>]  a CMake project built instead of the archive's own (gets -DXOURNAL_DEP_SOURCE_DIR=<src>)
#     [CMAKE_ARGS <args>...]  "<CONFIG>" is replaced by the configuration being built
#     [LICENSES <files>...]   license texts (relative to the source root) copied to <prefix>/licenses/<name>
#     [DOWNLOAD_ONLY])        no build: the archive's files, extracted to <base>/files
# Sets <name>_BASE (<root>/<name>-<version>-<key>; other recipes name <name>_BASE/<CONFIG>/... in their CMAKE_ARGS),
# <name>_PREFIX: <base>/$<CONFIG> (a generator expression for paths of built dependencies) or <base>/files, and
# <name>_PREFIX_<CONFIG> for each built configuration. Copies the license texts to <build>/licenses/<name>.
function(xournal_cached_dependency name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "DOWNLOAD_ONLY" "VERSION;URL;SHA256;SOURCE_WRAPPER;BUILD_TARGET;INSTALL_COMPONENT"
        "CMAKE_ARGS;LICENSES")
    foreach(required VERSION URL SHA256)
        if(NOT arg_${required})
            message(FATAL_ERROR "xournal_cached_dependency(${name}): ${required} missing")
        endif()
    endforeach()
    string(TOLOWER "${arg_SHA256}" sha256)
    set(recipe "${name}|${arg_VERSION}|${arg_URL}|${sha256}|${arg_DOWNLOAD_ONLY}|${arg_CMAKE_ARGS}|${arg_LICENSES}")
    string(APPEND recipe "|${arg_BUILD_TARGET}|${arg_INSTALL_COMPONENT}")
    if(arg_SOURCE_WRAPPER)
        file(GLOB_RECURSE wrapper_files "${arg_SOURCE_WRAPPER}/*")
        list(SORT wrapper_files)
        foreach(wrapper_file IN LISTS wrapper_files)
            file(SHA256 "${wrapper_file}" wrapper_hash)
            string(APPEND recipe "|${wrapper_hash}")
        endforeach()
    endif()
    if(NOT arg_DOWNLOAD_ONLY)
        string(APPEND recipe "|${_xournal_deps_toolchain}")
    else()
        string(APPEND recipe "|${_xournal_deps_cache_logic}")
    endif()
    string(SHA256 key "${recipe}")
    string(SUBSTRING "${key}" 0 16 key)
    set(base "${XOURNAL_DEPS_CACHE}/${name}-${arg_VERSION}-${key}")
    file(MAKE_DIRECTORY "${base}")
    file(TOUCH "${base}/last-used") # CI keeps only the folders its run used

    if(arg_DOWNLOAD_ONLY)
        set(configs files)
    else()
        set(configs ${XOURNAL_DEPS_CONFIGS})
    endif()
    set(options "") # the single-value options that are set (an empty value would be a CMP0174 warning)
    foreach(option SOURCE_WRAPPER BUILD_TARGET INSTALL_COMPONENT)
        if(arg_${option})
            list(APPEND options ${option} "${arg_${option}}")
        endif()
    endforeach()
    set(archive "")
    foreach(config IN LISTS configs)
        if(NOT EXISTS "${base}/${config}" AND NOT archive)
            _xournal_deps_download(${name} "${arg_URL}" "${sha256}" archive)
        endif()
        if(arg_DOWNLOAD_ONLY)
            if(NOT EXISTS "${base}/files")
                file(LOCK "${base}/files.lock" RESULT_VARIABLE lock_result TIMEOUT 7200)
                if(NOT lock_result STREQUAL "0")
                    message(FATAL_ERROR "Dependency cache: lock ${base}/files.lock: ${lock_result}")
                endif()
                if(NOT EXISTS "${base}/files")
                    _xournal_deps_extract("${archive}" "${base}/files.tmp" source)
                    file(RENAME "${source}" "${base}/files")
                    file(REMOVE_RECURSE "${base}/files.tmp")
                    message(STATUS "Dependency cache: ${name} extracted into ${base}/files")
                endif()
                file(LOCK "${base}/files.lock" RELEASE)
            endif()
        else()
            _xournal_deps_build_config(${name} "${base}" ${config} "${archive}" ${options}
                CMAKE_ARGS ${arg_CMAKE_ARGS} LICENSES ${arg_LICENSES})
        endif()
        set(${name}_PREFIX_${config} "${base}/${config}" PARENT_SCOPE)
    endforeach()

    if(arg_DOWNLOAD_ONLY)
        set(${name}_PREFIX "${base}/files" PARENT_SCOPE)
    else()
        set(${name}_PREFIX "${base}/$<CONFIG>" PARENT_SCOPE)
        list(GET configs 0 first)
        if(arg_LICENSES)
            file(COPY "${base}/${first}/licenses/${name}" DESTINATION "${CMAKE_BINARY_DIR}/licenses")
        endif()
    endif()
    if(arg_DOWNLOAD_ONLY AND arg_LICENSES)
        foreach(license IN LISTS arg_LICENSES)
            file(COPY "${base}/files/${license}" DESTINATION "${CMAKE_BINARY_DIR}/licenses/${name}")
        endforeach()
    endif()
    set(${name}_BASE "${base}" PARENT_SCOPE)
    message(STATUS "Dependency cache: ${name} ${arg_VERSION} -> ${base}")
endfunction()
