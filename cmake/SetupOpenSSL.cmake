# ==============================================================================
# SetupOpenSSL.cmake — Detect or auto-install OpenSSL to third_party/
# ==============================================================================
#
# Strategy (mirrors SetupZstd.cmake pattern):
#   1. Check third_party/openssl/{include,lib,bin} for pre-installed artifacts
#   2. If found  → create imported targets, done
#   3. If absent → download source tarball, build with perl+nmake,
#                   install headers+lib+bin+LICENSE to third_party/openssl/
#
# Output variables:
#   OPENSSL_INCLUDE_DIR  — path to OpenSSL headers
#   OPENSSL_SSL_LIB      — libssl library file path
#   OPENSSL_CRYPTO_LIB   — libcrypto library file path
#
# Output targets:
#   OpenSSL::SSL         — imported target for libssl
#   OpenSSL::Crypto      — imported target for libcrypto
# ==============================================================================

set(OPENSSL_VERSION     "3.5.0")
set(OPENSSL_THIRD_PARTY "${CMAKE_SOURCE_DIR}/third_party/openssl")
set(OPENSSL_TP_INCLUDE  "${OPENSSL_THIRD_PARTY}/include")
set(OPENSSL_TP_LIB      "${OPENSSL_THIRD_PARTY}/lib")
set(OPENSSL_TP_BIN      "${OPENSSL_THIRD_PARTY}/bin")

# ===========================================================================
# macOS: Use Homebrew
# ===========================================================================
if(APPLE)
    if(PLATFORM_ARCH STREQUAL "ARM64")
        set(_BREW_OPENSSL "/opt/homebrew/opt/openssl@3")
    else()
        set(_BREW_OPENSSL "/usr/local/opt/openssl@3")
    endif()

    if(EXISTS "${_BREW_OPENSSL}")
        set(OPENSSL_ROOT_DIR    "${_BREW_OPENSSL}" CACHE PATH "OpenSSL root (Homebrew)" FORCE)
        set(OPENSSL_INCLUDE_DIR "${_BREW_OPENSSL}/include" CACHE PATH "OpenSSL include" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY "${_BREW_OPENSSL}/lib/libcrypto.dylib" CACHE FILEPATH "OpenSSL crypto" FORCE)
        set(OPENSSL_SSL_LIBRARY    "${_BREW_OPENSSL}/lib/libssl.dylib"    CACHE FILEPATH "OpenSSL ssl" FORCE)
        message(STATUS "[OpenSSL] Found Homebrew OpenSSL at: ${_BREW_OPENSSL}")
    else()
        message(FATAL_ERROR "[OpenSSL] Not found. Install with: brew install openssl@3")
    endif()

    find_package(OpenSSL REQUIRED)
    return()
endif()

# ===========================================================================
# Linux: Use system package
# ===========================================================================
if(UNIX AND NOT APPLE)
    find_package(OpenSSL REQUIRED)
    message(STATUS "[OpenSSL] Using system OpenSSL: ${OPENSSL_VERSION}")
    return()
endif()

# ===========================================================================
# Windows: Check third_party/ → download source, build, install
# ===========================================================================
if(NOT WIN32)
    return()
endif()

# Platform-specific file names
set(_SSL_LIB    "${OPENSSL_TP_LIB}/libssl.lib")
set(_CRYPTO_LIB "${OPENSSL_TP_LIB}/libcrypto.lib")
set(_SSL_DLL    "${OPENSSL_TP_BIN}/libssl-3-x64.dll")
set(_CRYPTO_DLL "${OPENSSL_TP_BIN}/libcrypto-3-x64.dll")

# ---------------------------------------------------------------------------
# Helper: create imported targets from third_party/ artifacts
# ---------------------------------------------------------------------------
macro(_openssl_create_targets)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_TP_INCLUDE}")
    set(OPENSSL_SSL_LIB     "${_SSL_LIB}")
    set(OPENSSL_CRYPTO_LIB  "${_CRYPTO_LIB}")

    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL UNKNOWN IMPORTED)
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${_SSL_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_TP_INCLUDE}"
        )
    endif()

    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto UNKNOWN IMPORTED)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${_CRYPTO_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_TP_INCLUDE}"
        )
    endif()

    message(STATUS "[OpenSSL] include: ${OPENSSL_TP_INCLUDE}")
    message(STATUS "[OpenSSL] SSL lib: ${_SSL_LIB}")
    message(STATUS "[OpenSSL] Crypto:  ${_CRYPTO_LIB}")
endmacro()

# ---------------------------------------------------------------------------
# 1) Check pre-installed artifacts in third_party/
# ---------------------------------------------------------------------------
if(EXISTS "${OPENSSL_TP_INCLUDE}/openssl/ssl.h"
   AND EXISTS "${_SSL_LIB}"
   AND EXISTS "${_CRYPTO_LIB}")

    message(STATUS "[OpenSSL] Using pre-installed OpenSSL from: ${OPENSSL_THIRD_PARTY}")
    _openssl_create_targets()
    return()
endif()

# ---------------------------------------------------------------------------
# 2) Not found → download source, build, and install
# ---------------------------------------------------------------------------
message(STATUS "[OpenSSL] Pre-built not found, downloading and building v${OPENSSL_VERSION}...")

set(_OPENSSL_TARBALL      "openssl-${OPENSSL_VERSION}.tar.gz")
set(_OPENSSL_URL          "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${_OPENSSL_TARBALL}")
set(_OPENSSL_DL_DIR       "${CMAKE_BINARY_DIR}/_deps/openssl-download")
set(_OPENSSL_SRC_DIR      "${CMAKE_BINARY_DIR}/_deps/openssl-src")
set(_OPENSSL_TARBALL_PATH "${_OPENSSL_DL_DIR}/${_OPENSSL_TARBALL}")

# Download tarball if not cached
if(NOT EXISTS "${_OPENSSL_TARBALL_PATH}")
    file(MAKE_DIRECTORY "${_OPENSSL_DL_DIR}")
    # Mirror list: ghfast proxy for CN users, then direct GitHub
    set(_OPENSSL_URLS
        "https://ghfast.top/${_OPENSSL_URL}"
        "${_OPENSSL_URL}"
    )
    set(_dl_success FALSE)
    foreach(_url ${_OPENSSL_URLS})
        message(STATUS "[OpenSSL] Downloading ${_url} ...")
        file(DOWNLOAD "${_url}" "${_OPENSSL_TARBALL_PATH}"
             STATUS _dl_status
             SHOW_PROGRESS
             TIMEOUT 120
        )
        list(GET _dl_status 0 _dl_code)
        if(_dl_code EQUAL 0)
            file(SIZE "${_OPENSSL_TARBALL_PATH}" _dl_size)
            if(_dl_size GREATER 100000)
                set(_dl_success TRUE)
                break()
            endif()
        endif()
        file(REMOVE "${_OPENSSL_TARBALL_PATH}")
        message(STATUS "[OpenSSL] Download failed from ${_url}, trying next mirror...")
    endforeach()
    if(NOT _dl_success)
        message(FATAL_ERROR
            "[OpenSSL] All download mirrors failed.\n"
            "  Please manually download ${_OPENSSL_TARBALL} and place it at:\n"
            "  ${_OPENSSL_TARBALL_PATH}"
        )
    endif()
endif()

# Extract tarball if source not present
if(NOT EXISTS "${_OPENSSL_SRC_DIR}/include/openssl/ssl.h.in")
    message(STATUS "[OpenSSL] Extracting ${_OPENSSL_TARBALL} ...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf "${_OPENSSL_TARBALL_PATH}"
        WORKING_DIRECTORY "${_OPENSSL_DL_DIR}"
        RESULT_VARIABLE _extract_result
    )
    if(NOT _extract_result EQUAL 0)
        message(FATAL_ERROR "[OpenSSL] Failed to extract tarball")
    endif()
    # The tarball extracts to openssl-<version>/ subdirectory; move contents
    file(GLOB _extracted_dirs "${_OPENSSL_DL_DIR}/openssl-*")
    list(GET _extracted_dirs 0 _extracted_dir)
    if(EXISTS "${_extracted_dir}/Configure")
        if(EXISTS "${_OPENSSL_SRC_DIR}")
            file(REMOVE_RECURSE "${_OPENSSL_SRC_DIR}")
        endif()
        file(RENAME "${_extracted_dir}" "${_OPENSSL_SRC_DIR}")
    else()
        message(FATAL_ERROR "[OpenSSL] Extracted directory does not contain Configure: ${_extracted_dir}")
    endif()
endif()

# Build OpenSSL using perl + nmake
if(NOT EXISTS "${_SSL_LIB}")
    message(STATUS "[OpenSSL] Building from source (this may take several minutes)...")

    # --- Prerequisite: Perl ---
    find_program(_PERL_EXE perl)
    if(NOT _PERL_EXE)
        message(FATAL_ERROR
            "[OpenSSL] Perl not found (required to build OpenSSL from source).\n"
            "  Install Strawberry Perl: https://strawberryperl.com/"
        )
    endif()
    message(STATUS "[OpenSSL] Using Perl: ${_PERL_EXE}")

    # --- Locate vcvarsall.bat from MSVC compiler path ---
    # CMAKE_CXX_COMPILER = .../VC/Tools/MSVC/<ver>/bin/Hostx64/x64/cl.exe
    get_filename_component(_CL_REAL "${CMAKE_CXX_COMPILER}" REALPATH)
    string(REPLACE "\\" "/" _CL_REAL "${_CL_REAL}")
    string(REGEX REPLACE "/VC/Tools/.*" "" _VS_ROOT "${_CL_REAL}")
    set(_VCVARSALL "${_VS_ROOT}/VC/Auxiliary/Build/vcvarsall.bat")
    if(NOT EXISTS "${_VCVARSALL}")
        message(FATAL_ERROR "[OpenSSL] vcvarsall.bat not found at: ${_VCVARSALL}")
    endif()
    message(STATUS "[OpenSSL] Using vcvarsall: ${_VCVARSALL}")

    # --- Configure and build (single cmd invocation for VC environment) ---
    # no-asm: skip NASM dependency; builds slightly slower but no extra tools needed
    string(REPLACE "/" "\\" _SRC_WIN "${_OPENSSL_SRC_DIR}")
    execute_process(
        COMMAND cmd /c "\"${_VCVARSALL}\" x64 && cd /d \"${_SRC_WIN}\" && perl Configure VC-WIN64A no-asm && nmake build_libs"
        RESULT_VARIABLE _build_result
        OUTPUT_VARIABLE _build_output
        ERROR_VARIABLE  _build_error
    )
    if(NOT _build_result EQUAL 0)
        message(FATAL_ERROR "[OpenSSL] Build failed:\n${_build_error}")
    endif()

    # --- Install headers to third_party/ ---
    file(MAKE_DIRECTORY "${OPENSSL_TP_INCLUDE}")
    file(COPY "${_OPENSSL_SRC_DIR}/include/openssl" DESTINATION "${OPENSSL_TP_INCLUDE}")

    # --- Install libraries ---
    file(MAKE_DIRECTORY "${OPENSSL_TP_LIB}")
    file(COPY "${_OPENSSL_SRC_DIR}/libssl.lib"    DESTINATION "${OPENSSL_TP_LIB}")
    file(COPY "${_OPENSSL_SRC_DIR}/libcrypto.lib" DESTINATION "${OPENSSL_TP_LIB}")

    # --- Install runtime DLLs ---
    file(MAKE_DIRECTORY "${OPENSSL_TP_BIN}")
    if(EXISTS "${_OPENSSL_SRC_DIR}/libssl-3-x64.dll")
        file(COPY "${_OPENSSL_SRC_DIR}/libssl-3-x64.dll"    DESTINATION "${OPENSSL_TP_BIN}")
        file(COPY "${_OPENSSL_SRC_DIR}/libcrypto-3-x64.dll" DESTINATION "${OPENSSL_TP_BIN}")
    endif()

    # --- Install LICENSE ---
    if(EXISTS "${_OPENSSL_SRC_DIR}/LICENSE.txt")
        file(COPY "${_OPENSSL_SRC_DIR}/LICENSE.txt" DESTINATION "${OPENSSL_THIRD_PARTY}")
    endif()

    message(STATUS "[OpenSSL] Installed to: ${OPENSSL_THIRD_PARTY}")
endif()

# Use the installed artifacts
_openssl_create_targets()
