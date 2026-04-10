# ==============================================================================
# SetupZstd.cmake — Detect or auto-build zstd and install to third_party/
# ==============================================================================
#
# Strategy:
#   1. Check third_party/zstd/{include,lib} for pre-built artifacts
#   2. If found  → create imported target, set ZSTD_INCLUDE_DIR / ZSTD_LIBRARY
#   3. If absent → download source tarball, build static lib via cmake,
#                   install headers+lib+LICENSE to third_party/zstd/
#
# Output variables:
#   ZSTD_INCLUDE_DIR  — path to zstd headers
#   ZSTD_LIBRARY      — library to link (file path or target name)
# ==============================================================================

set(ZSTD_VERSION      "1.5.6")
set(ZSTD_THIRD_PARTY  "${CMAKE_SOURCE_DIR}/third_party/zstd")
set(ZSTD_TP_INCLUDE   "${ZSTD_THIRD_PARTY}/include")
set(ZSTD_TP_LIB       "${ZSTD_THIRD_PARTY}/lib")

# Platform-specific library file name
if(WIN32)
    set(_ZSTD_LIB_NAME "zstd_static.lib")
else()
    set(_ZSTD_LIB_NAME "libzstd.a")
endif()
set(_ZSTD_LIB_FILE "${ZSTD_TP_LIB}/${_ZSTD_LIB_NAME}")

# ---------------------------------------------------------------------------
# 1) Try pre-built artifacts in third_party/
# ---------------------------------------------------------------------------
if(EXISTS "${ZSTD_TP_INCLUDE}/zstd.h" AND EXISTS "${_ZSTD_LIB_FILE}")
    message(STATUS "[zstd] Using pre-built zstd from: ${ZSTD_THIRD_PARTY}")

    set(ZSTD_INCLUDE_DIR "${ZSTD_TP_INCLUDE}")
    set(ZSTD_LIBRARY     "${_ZSTD_LIB_FILE}")

    message(STATUS "[zstd] include: ${ZSTD_INCLUDE_DIR}")
    message(STATUS "[zstd] library: ${ZSTD_LIBRARY}")
    return()
endif()

# ---------------------------------------------------------------------------
# 2) Not found → download, build, and install
# ---------------------------------------------------------------------------
message(STATUS "[zstd] Pre-built not found, downloading and building v${ZSTD_VERSION}...")

set(_ZSTD_TARBALL     "zstd-${ZSTD_VERSION}.tar.gz")
set(_ZSTD_URL         "https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/${_ZSTD_TARBALL}")
set(_ZSTD_DL_DIR      "${CMAKE_BINARY_DIR}/_deps/zstd-download")
set(_ZSTD_SRC_DIR     "${CMAKE_BINARY_DIR}/_deps/zstd-src")
set(_ZSTD_BUILD_DIR   "${CMAKE_BINARY_DIR}/_deps/zstd-build")
set(_ZSTD_TARBALL_PATH "${_ZSTD_DL_DIR}/${_ZSTD_TARBALL}")

# Download tarball if not cached
if(NOT EXISTS "${_ZSTD_TARBALL_PATH}")
    file(MAKE_DIRECTORY "${_ZSTD_DL_DIR}")
    # Mirror list: try GitHub first, then ghproxy mirror for CN users
    set(_ZSTD_URLS
        "${_ZSTD_URL}"
        "https://ghfast.top/https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/${_ZSTD_TARBALL}"
    )
    set(_dl_success FALSE)
    foreach(_url ${_ZSTD_URLS})
        message(STATUS "[zstd] Downloading ${_url} ...")
        file(DOWNLOAD "${_url}" "${_ZSTD_TARBALL_PATH}"
             STATUS _dl_status
             SHOW_PROGRESS
             TIMEOUT 120
        )
        list(GET _dl_status 0 _dl_code)
        if(_dl_code EQUAL 0)
            # Verify the file is not empty (partial download)
            file(SIZE "${_ZSTD_TARBALL_PATH}" _dl_size)
            if(_dl_size GREATER 100000)
                set(_dl_success TRUE)
                break()
            endif()
        endif()
        file(REMOVE "${_ZSTD_TARBALL_PATH}")
        message(STATUS "[zstd] Download failed from ${_url}, trying next mirror...")
    endforeach()
    if(NOT _dl_success)
        message(FATAL_ERROR
            "[zstd] All download mirrors failed.\n"
            "  Please manually download zstd-${ZSTD_VERSION}.tar.gz and place it at:\n"
            "  ${_ZSTD_TARBALL_PATH}"
        )
    endif()
endif()

# Extract tarball if source not present
if(NOT EXISTS "${_ZSTD_SRC_DIR}/lib/zstd.h")
    message(STATUS "[zstd] Extracting ${_ZSTD_TARBALL} ...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${_ZSTD_TARBALL_PATH}"
        WORKING_DIRECTORY "${_ZSTD_DL_DIR}"
        RESULT_VARIABLE _extract_result
    )
    if(NOT _extract_result EQUAL 0)
        message(FATAL_ERROR "[zstd] Failed to extract tarball")
    endif()
    # The tarball extracts to zstd-<version>/ subdirectory; move contents
    file(GLOB _extracted_dirs "${_ZSTD_DL_DIR}/zstd-*")
    list(GET _extracted_dirs 0 _extracted_dir)
    if(EXISTS "${_extracted_dir}/lib/zstd.h")
        # Remove any stale target directory before rename (Windows does not allow rename-over)
        if(EXISTS "${_ZSTD_SRC_DIR}")
            file(REMOVE_RECURSE "${_ZSTD_SRC_DIR}")
        endif()
        file(RENAME "${_extracted_dir}" "${_ZSTD_SRC_DIR}")
    else()
        message(FATAL_ERROR "[zstd] Extracted directory does not contain lib/zstd.h: ${_extracted_dir}")
    endif()
endif()

# Build zstd static library using cmake subprocess
if(NOT EXISTS "${_ZSTD_LIB_FILE}")
    message(STATUS "[zstd] Building static library...")
    file(MAKE_DIRECTORY "${_ZSTD_BUILD_DIR}")

    # Build platform args for multi-config generators (VS)
    set(_ZSTD_GEN_ARGS -G "${CMAKE_GENERATOR}")
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND _ZSTD_GEN_ARGS -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()

    # Configure
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            -S "${_ZSTD_SRC_DIR}/build/cmake"
            -B "${_ZSTD_BUILD_DIR}"
            ${_ZSTD_GEN_ARGS}
            -DCMAKE_BUILD_TYPE=Release
            -DZSTD_BUILD_PROGRAMS=OFF
            -DZSTD_BUILD_SHARED=OFF
            -DZSTD_BUILD_STATIC=ON
            -DZSTD_BUILD_TESTS=OFF
        RESULT_VARIABLE _cfg_result
        OUTPUT_VARIABLE _cfg_output
        ERROR_VARIABLE  _cfg_error
    )
    if(NOT _cfg_result EQUAL 0)
        message(FATAL_ERROR "[zstd] Configure failed:\n${_cfg_error}")
    endif()

    # Build
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${_ZSTD_BUILD_DIR}" --config Release --target libzstd_static
        RESULT_VARIABLE _build_result
        OUTPUT_VARIABLE _build_output
        ERROR_VARIABLE  _build_error
    )
    if(NOT _build_result EQUAL 0)
        message(FATAL_ERROR "[zstd] Build failed:\n${_build_error}")
    endif()

    # Install headers to third_party/
    file(MAKE_DIRECTORY "${ZSTD_TP_INCLUDE}")
    file(COPY
        "${_ZSTD_SRC_DIR}/lib/zstd.h"
        "${_ZSTD_SRC_DIR}/lib/zstd_errors.h"
        "${_ZSTD_SRC_DIR}/lib/zdict.h"
        DESTINATION "${ZSTD_TP_INCLUDE}"
    )

    # Install compiled library
    file(MAKE_DIRECTORY "${ZSTD_TP_LIB}")
    # Find the built library (could be in Release/ subdir for multi-config generators)
    file(GLOB_RECURSE _built_libs "${_ZSTD_BUILD_DIR}/lib/*zstd_static*")
    if(_built_libs)
        list(GET _built_libs 0 _built_lib)
        file(COPY "${_built_lib}" DESTINATION "${ZSTD_TP_LIB}")
        # Normalize the file name
        get_filename_component(_copied_name "${_built_lib}" NAME)
        if(NOT "${_copied_name}" STREQUAL "${_ZSTD_LIB_NAME}")
            file(RENAME "${ZSTD_TP_LIB}/${_copied_name}" "${_ZSTD_LIB_FILE}")
        endif()
    else()
        message(FATAL_ERROR "[zstd] Could not find built library in ${_ZSTD_BUILD_DIR}")
    endif()

    # Install LICENSE / COPYING
    if(EXISTS "${_ZSTD_SRC_DIR}/LICENSE")
        file(COPY "${_ZSTD_SRC_DIR}/LICENSE" DESTINATION "${ZSTD_THIRD_PARTY}")
    endif()
    if(EXISTS "${_ZSTD_SRC_DIR}/COPYING")
        file(COPY "${_ZSTD_SRC_DIR}/COPYING" DESTINATION "${ZSTD_THIRD_PARTY}")
    endif()

    message(STATUS "[zstd] Installed to: ${ZSTD_THIRD_PARTY}")
endif()

# Use the installed artifacts
set(ZSTD_INCLUDE_DIR "${ZSTD_TP_INCLUDE}")
set(ZSTD_LIBRARY     "${_ZSTD_LIB_FILE}")

message(STATUS "[zstd] include: ${ZSTD_INCLUDE_DIR}")
message(STATUS "[zstd] library: ${ZSTD_LIBRARY}")
