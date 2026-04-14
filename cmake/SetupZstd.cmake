# SetupZstd.cmake — Detect or auto-build zstd into third_party/
#
# 1. Check third_party/zstd/{include,lib/<platform>} for pre-built artifacts
# 2. Found  → create imported target zstd::zstd
# 3. Absent → download source, build static lib, install to third_party/
#
# Requires PLATFORM_NAME and PLATFORM_ARCH from parent CMakeLists.txt.
#
# Output: zstd::zstd (imported static, per-config), ZSTD_INCLUDE_DIR

set(ZSTD_VERSION      "1.5.6")
set(ZSTD_THIRD_PARTY  "${CMAKE_SOURCE_DIR}/third_party/zstd")
set(ZSTD_TP_INCLUDE   "${ZSTD_THIRD_PARTY}/include")
set(ZSTD_TP_LIB       "${ZSTD_THIRD_PARTY}/lib/${PLATFORM_NAME}-${PLATFORM_ARCH}")

if(WIN32)
    set(_ZSTD_REL "zstd_static.lib")
    set(_ZSTD_DBG "zstd_staticD.lib")
else()
    set(_ZSTD_REL "libzstd.a")
    set(_ZSTD_DBG "libzstdD.a")
endif()

# -- Helper: create imported target ----------------------------------------
macro(_zstd_create_target)
    if(NOT TARGET zstd::zstd)
        add_library(zstd::zstd STATIC IMPORTED GLOBAL)
        set_target_properties(zstd::zstd PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES    "${ZSTD_TP_INCLUDE}"
            IMPORTED_LOCATION_DEBUG          "${ZSTD_TP_LIB}/${_ZSTD_DBG}"
            IMPORTED_LOCATION_RELEASE        "${ZSTD_TP_LIB}/${_ZSTD_REL}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${ZSTD_TP_LIB}/${_ZSTD_REL}"
            IMPORTED_LOCATION_MINSIZEREL     "${ZSTD_TP_LIB}/${_ZSTD_REL}"
        )
    endif()
    set(ZSTD_INCLUDE_DIR "${ZSTD_TP_INCLUDE}")
endmacro()

# -- Helper: locate built lib and copy to third_party/ with target name ----
function(_zstd_install_lib BUILD_DIR CONFIG DEST_NAME)
    file(MAKE_DIRECTORY "${ZSTD_TP_LIB}")
    # Multi-config (VS): lib/<Config>/  |  Single-config: lib/
    set(_candidates
        "${BUILD_DIR}/lib/${CONFIG}/zstd_static.lib"
        "${BUILD_DIR}/lib/${CONFIG}/libzstd.a"
        "${BUILD_DIR}/lib/libzstd.a"
    )
    set(_found "")
    foreach(_p IN LISTS _candidates)
        if(EXISTS "${_p}")
            set(_found "${_p}")
            break()
        endif()
    endforeach()
    if(NOT _found)
        message(FATAL_ERROR "[zstd] ${CONFIG} lib not found in ${BUILD_DIR}")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${_found}" "${ZSTD_TP_LIB}/${DEST_NAME}")
    message(STATUS "[zstd]   ${CONFIG}: ${_found} -> ${DEST_NAME}")
endfunction()

# ==========================================================================
# 1) Use pre-built if both Debug+Release exist
# ==========================================================================
if(EXISTS "${ZSTD_TP_INCLUDE}/zstd.h"
   AND EXISTS "${ZSTD_TP_LIB}/${_ZSTD_DBG}"
   AND EXISTS "${ZSTD_TP_LIB}/${_ZSTD_REL}")
    message(STATUS "[zstd] Using pre-built from ${ZSTD_THIRD_PARTY}")
    _zstd_create_target()
    return()
endif()

# ==========================================================================
# 2) Download source and build
# ==========================================================================
message(STATUS "[zstd] Pre-built not found — downloading v${ZSTD_VERSION}...")

set(_DL_DIR   "${CMAKE_BINARY_DIR}/_deps/zstd-download")
set(_SRC_DIR  "${CMAKE_BINARY_DIR}/_deps/zstd-src")
set(_TARBALL  "${_DL_DIR}/zstd-${ZSTD_VERSION}.tar.gz")

# Download
if(NOT EXISTS "${_TARBALL}")
    file(MAKE_DIRECTORY "${_DL_DIR}")
    set(_URLS
        "https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/zstd-${ZSTD_VERSION}.tar.gz"
        "https://ghfast.top/https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/zstd-${ZSTD_VERSION}.tar.gz"
    )
    set(_ok FALSE)
    foreach(_url IN LISTS _URLS)
        message(STATUS "[zstd] Trying ${_url}")
        file(DOWNLOAD "${_url}" "${_TARBALL}" STATUS _st SHOW_PROGRESS TIMEOUT 120)
        list(GET _st 0 _code)
        if(_code EQUAL 0)
            file(SIZE "${_TARBALL}" _sz)
            if(_sz GREATER 100000)
                set(_ok TRUE)
                break()
            endif()
        endif()
        file(REMOVE "${_TARBALL}")
    endforeach()
    if(NOT _ok)
        message(FATAL_ERROR "[zstd] Download failed. Place zstd-${ZSTD_VERSION}.tar.gz at: ${_TARBALL}")
    endif()
endif()

# Extract
if(NOT EXISTS "${_SRC_DIR}/lib/zstd.h")
    message(STATUS "[zstd] Extracting...")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${_TARBALL}"
                    WORKING_DIRECTORY "${_DL_DIR}" RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "[zstd] Extract failed")
    endif()
    file(GLOB _dirs "${_DL_DIR}/zstd-*")
    list(GET _dirs 0 _dir)
    if(EXISTS "${_SRC_DIR}")
        file(REMOVE_RECURSE "${_SRC_DIR}")
    endif()
    file(RENAME "${_dir}" "${_SRC_DIR}")
endif()

# Build (only configs not yet installed)
set(_ZSTD_CMAKE_ARGS
    -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_SHARED=OFF
    -DZSTD_BUILD_STATIC=ON   -DZSTD_BUILD_TESTS=OFF
)

if(MSVC)
    # Multi-config: one configure, two --config builds
    set(_BD "${CMAKE_BINARY_DIR}/_deps/zstd-build")
    set(_GEN -G "${CMAKE_GENERATOR}")
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND _GEN -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -S "${_SRC_DIR}/build/cmake" -B "${_BD}" ${_GEN} ${_ZSTD_CMAKE_ARGS}
        RESULT_VARIABLE _r OUTPUT_QUIET ERROR_VARIABLE _e)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "[zstd] Configure failed:\n${_e}")
    endif()
    foreach(_cfg Debug Release)
        if(_cfg STREQUAL "Debug")
            set(_dest "${_ZSTD_DBG}")
        else()
            set(_dest "${_ZSTD_REL}")
        endif()
        if(NOT EXISTS "${ZSTD_TP_LIB}/${_dest}")
            message(STATUS "[zstd] Building ${_cfg}...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build "${_BD}" --config ${_cfg} --target libzstd_static
                RESULT_VARIABLE _r OUTPUT_QUIET ERROR_VARIABLE _e)
            if(NOT _r EQUAL 0)
                message(FATAL_ERROR "[zstd] ${_cfg} build failed:\n${_e}")
            endif()
            _zstd_install_lib("${_BD}" "${_cfg}" "${_dest}")
        endif()
    endforeach()
else()
    # Single-config: separate build dirs per config
    foreach(_cfg Debug Release)
        if(_cfg STREQUAL "Debug")
            set(_dest "${_ZSTD_DBG}")
        else()
            set(_dest "${_ZSTD_REL}")
        endif()
        if(NOT EXISTS "${ZSTD_TP_LIB}/${_dest}")
            set(_BD "${CMAKE_BINARY_DIR}/_deps/zstd-build-${_cfg}")
            message(STATUS "[zstd] Configuring+building ${_cfg}...")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -S "${_SRC_DIR}/build/cmake" -B "${_BD}"
                        -DCMAKE_BUILD_TYPE=${_cfg} ${_ZSTD_CMAKE_ARGS}
                RESULT_VARIABLE _r OUTPUT_QUIET ERROR_VARIABLE _e)
            if(NOT _r EQUAL 0)
                message(FATAL_ERROR "[zstd] ${_cfg} configure failed:\n${_e}")
            endif()
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build "${_BD}" --target libzstd_static
                RESULT_VARIABLE _r OUTPUT_QUIET ERROR_VARIABLE _e)
            if(NOT _r EQUAL 0)
                message(FATAL_ERROR "[zstd] ${_cfg} build failed:\n${_e}")
            endif()
            _zstd_install_lib("${_BD}" "${_cfg}" "${_dest}")
        endif()
    endforeach()
endif()

# Install headers + license
file(MAKE_DIRECTORY "${ZSTD_TP_INCLUDE}")
file(COPY "${_SRC_DIR}/lib/zstd.h" "${_SRC_DIR}/lib/zstd_errors.h" "${_SRC_DIR}/lib/zdict.h"
     DESTINATION "${ZSTD_TP_INCLUDE}")
foreach(_lic LICENSE COPYING)
    if(EXISTS "${_SRC_DIR}/${_lic}")
        file(COPY "${_SRC_DIR}/${_lic}" DESTINATION "${ZSTD_THIRD_PARTY}")
    endif()
endforeach()
message(STATUS "[zstd] Installed to ${ZSTD_THIRD_PARTY}")

_zstd_create_target()
