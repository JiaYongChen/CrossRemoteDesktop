# SetupOpenSSL.cmake — Detect or auto-build OpenSSL into third_party/
#
# 1. Check third_party/openssl/{include,lib/<platform>,bin/<platform>}
# 2. Found  → create imported targets OpenSSL::SSL, OpenSSL::Crypto
# 3. Absent → download source, build with Perl/nmake (Win) or make (Unix)
#
# Windows special: if Release exists but Debug missing, D-suffix copies are
# created from Release (DLL import libs have no CRT code — safe to share).
#
# Requires PLATFORM_NAME and PLATFORM_ARCH from parent CMakeLists.txt.
#
# Output: OpenSSL::SSL, OpenSSL::Crypto (imported, per-config)
#         OPENSSL_INCLUDE_DIR, OPENSSL_TP_BIN

set(OPENSSL_VERSION     "3.6.2")
set(OPENSSL_THIRD_PARTY "${CMAKE_SOURCE_DIR}/third_party/openssl")
set(OPENSSL_TP_INCLUDE  "${OPENSSL_THIRD_PARTY}/include")
set(OPENSSL_TP_LIB      "${OPENSSL_THIRD_PARTY}/lib/${PLATFORM_NAME}-${PLATFORM_ARCH}")
set(OPENSSL_TP_BIN      "${OPENSSL_THIRD_PARTY}/bin/${PLATFORM_NAME}-${PLATFORM_ARCH}")

if(WIN32)
    set(_SSL_REL    "libssl.lib")
    set(_SSL_DBG    "libsslD.lib")
    set(_CRYPTO_REL "libcrypto.lib")
    set(_CRYPTO_DBG "libcryptoD.lib")
    set(_SSL_DLL_REL    "libssl-3-x64.dll")
    set(_SSL_DLL_DBG    "libssl-3-x64D.dll")
    set(_CRYPTO_DLL_REL "libcrypto-3-x64.dll")
    set(_CRYPTO_DLL_DBG "libcrypto-3-x64D.dll")
else()
    set(_SSL_REL    "libssl.a")
    set(_SSL_DBG    "libsslD.a")
    set(_CRYPTO_REL "libcrypto.a")
    set(_CRYPTO_DBG "libcryptoD.a")
endif()

# -- Helper: create imported targets --------------------------------------
macro(_openssl_create_targets)
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_TP_INCLUDE}")
    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL UNKNOWN IMPORTED GLOBAL)
        set_target_properties(OpenSSL::SSL PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES    "${OPENSSL_TP_INCLUDE}"
            IMPORTED_LOCATION_DEBUG          "${OPENSSL_TP_LIB}/${_SSL_DBG}"
            IMPORTED_LOCATION_RELEASE        "${OPENSSL_TP_LIB}/${_SSL_REL}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${OPENSSL_TP_LIB}/${_SSL_REL}"
            IMPORTED_LOCATION_MINSIZEREL     "${OPENSSL_TP_LIB}/${_SSL_REL}"
        )
    endif()
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto UNKNOWN IMPORTED GLOBAL)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES    "${OPENSSL_TP_INCLUDE}"
            IMPORTED_LOCATION_DEBUG          "${OPENSSL_TP_LIB}/${_CRYPTO_DBG}"
            IMPORTED_LOCATION_RELEASE        "${OPENSSL_TP_LIB}/${_CRYPTO_REL}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${OPENSSL_TP_LIB}/${_CRYPTO_REL}"
            IMPORTED_LOCATION_MINSIZEREL     "${OPENSSL_TP_LIB}/${_CRYPTO_REL}"
        )
    endif()
endmacro()

# ==========================================================================
# Windows: create Debug copies from Release if only Release exists.
# OpenSSL uses DLL linking — import libs are thin stubs with no CRT code.
# ==========================================================================
if(WIN32
   AND EXISTS "${OPENSSL_TP_INCLUDE}/openssl/ssl.h"
   AND EXISTS "${OPENSSL_TP_LIB}/${_SSL_REL}"
   AND EXISTS "${OPENSSL_TP_LIB}/${_CRYPTO_REL}")
    foreach(_pair "${_SSL_REL};${_SSL_DBG}" "${_CRYPTO_REL};${_CRYPTO_DBG}")
        list(GET _pair 0 _src)
        list(GET _pair 1 _dst)
        if(NOT EXISTS "${OPENSSL_TP_LIB}/${_dst}")
            execute_process(COMMAND ${CMAKE_COMMAND} -E copy
                "${OPENSSL_TP_LIB}/${_src}" "${OPENSSL_TP_LIB}/${_dst}")
        endif()
    endforeach()
    foreach(_pair "${_SSL_DLL_REL};${_SSL_DLL_DBG}" "${_CRYPTO_DLL_REL};${_CRYPTO_DLL_DBG}")
        list(GET _pair 0 _src)
        list(GET _pair 1 _dst)
        if(EXISTS "${OPENSSL_TP_BIN}/${_src}" AND NOT EXISTS "${OPENSSL_TP_BIN}/${_dst}")
            execute_process(COMMAND ${CMAKE_COMMAND} -E copy
                "${OPENSSL_TP_BIN}/${_src}" "${OPENSSL_TP_BIN}/${_dst}")
        endif()
    endforeach()
endif()

# ==========================================================================
# 1) Use pre-built if all artifacts exist
# ==========================================================================
set(_ok TRUE)
if(NOT EXISTS "${OPENSSL_TP_INCLUDE}/openssl/ssl.h")
    set(_ok FALSE)
endif()
foreach(_f "${_SSL_DBG}" "${_SSL_REL}" "${_CRYPTO_DBG}" "${_CRYPTO_REL}")
    if(NOT EXISTS "${OPENSSL_TP_LIB}/${_f}")
        set(_ok FALSE)
    endif()
endforeach()
if(WIN32)
    foreach(_f "${_SSL_DLL_DBG}" "${_SSL_DLL_REL}" "${_CRYPTO_DLL_DBG}" "${_CRYPTO_DLL_REL}")
        if(NOT EXISTS "${OPENSSL_TP_BIN}/${_f}")
            set(_ok FALSE)
        endif()
    endforeach()
endif()

if(_ok)
    message(STATUS "[OpenSSL] Using pre-built from ${OPENSSL_THIRD_PARTY}")
    _openssl_create_targets()
    return()
endif()

# ==========================================================================
# 2) Download source and build
# ==========================================================================
message(STATUS "[OpenSSL] Pre-built not found — downloading v${OPENSSL_VERSION}...")

set(_DL_DIR   "${CMAKE_BINARY_DIR}/_deps/openssl-download")
set(_SRC_DIR  "${CMAKE_BINARY_DIR}/_deps/openssl-src")
set(_TARBALL  "${_DL_DIR}/openssl-${OPENSSL_VERSION}.tar.gz")

# Download
if(NOT EXISTS "${_TARBALL}")
    file(MAKE_DIRECTORY "${_DL_DIR}")
    set(_BASE "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz")
    set(_URLS "${_BASE}" "https://ghfast.top/${_BASE}")
    set(_dl_ok FALSE)
    foreach(_url IN LISTS _URLS)
        message(STATUS "[OpenSSL] Trying ${_url}")
        file(DOWNLOAD "${_url}" "${_TARBALL}" STATUS _st SHOW_PROGRESS TIMEOUT 120)
        list(GET _st 0 _code)
        if(_code EQUAL 0)
            file(SIZE "${_TARBALL}" _sz)
            if(_sz GREATER 100000)
                set(_dl_ok TRUE)
                break()
            endif()
        endif()
        file(REMOVE "${_TARBALL}")
    endforeach()
    if(NOT _dl_ok)
        message(FATAL_ERROR "[OpenSSL] Download failed. Place openssl-${OPENSSL_VERSION}.tar.gz at: ${_TARBALL}")
    endif()
endif()

# Extract
if(NOT EXISTS "${_SRC_DIR}/Configure")
    message(STATUS "[OpenSSL] Extracting...")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${_TARBALL}"
                    WORKING_DIRECTORY "${_DL_DIR}" RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "[OpenSSL] Extract failed")
    endif()
    file(GLOB _dirs "${_DL_DIR}/openssl-*")
    list(GET _dirs 0 _dir)
    if(EXISTS "${_SRC_DIR}")
        file(REMOVE_RECURSE "${_SRC_DIR}")
    endif()
    file(RENAME "${_dir}" "${_SRC_DIR}")
endif()

# -- Prerequisites ---------------------------------------------------------
# Perl (Strawberry on Windows; Git perl lacks required modules)
if(WIN32)
    find_program(_PERL perl PATHS "C:/Strawberry/perl/bin" NO_DEFAULT_PATH)
    if(NOT _PERL)
        find_program(_PERL perl)
        if(_PERL AND _PERL MATCHES "Git")
            message(FATAL_ERROR
                "[OpenSSL] Git perl (${_PERL}) lacks required modules.\n"
                "  Install Strawberry Perl: https://strawberryperl.com/\n"
                "  Or place pre-built libs in: ${OPENSSL_TP_LIB}/")
        endif()
    endif()
else()
    find_program(_PERL perl)
endif()
if(NOT _PERL)
    message(FATAL_ERROR "[OpenSSL] Perl not found (required to build from source)")
endif()

include(ProcessorCount)
ProcessorCount(_NPROC)
if(_NPROC EQUAL 0)
    set(_NPROC 4)
endif()

if(WIN32)
    get_filename_component(_CL "${CMAKE_CXX_COMPILER}" REALPATH)
    string(REPLACE "\\" "/" _CL "${_CL}")
    string(REGEX REPLACE "/VC/Tools/.*" "" _VS "${_CL}")
    set(_VCVARS "${_VS}/VC/Auxiliary/Build/vcvarsall.bat")
    if(NOT EXISTS "${_VCVARS}")
        message(FATAL_ERROR "[OpenSSL] vcvarsall.bat not found: ${_VCVARS}")
    endif()
else()
    if(APPLE)
        if(PLATFORM_ARCH STREQUAL "ARM64")
            set(_TARGET "darwin64-arm64-cc")
        else()
            set(_TARGET "darwin64-x86_64-cc")
        endif()
    else()
        if(PLATFORM_ARCH STREQUAL "ARM64")
            set(_TARGET "linux-aarch64")
        else()
            set(_TARGET "linux-x86_64")
        endif()
    endif()
    find_program(_MAKE make)
    if(NOT _MAKE)
        message(FATAL_ERROR "[OpenSSL] make not found")
    endif()
endif()

# -- Helper: build one config and install artifacts ------------------------
function(_openssl_build CONFIG SSL_DEST CRYPTO_DEST)
    set(_BD "${CMAKE_BINARY_DIR}/_deps/openssl-build-${CONFIG}")
    if(NOT EXISTS "${_BD}/Configure")
        message(STATUS "[OpenSSL] Copying source for ${CONFIG}...")
        if(EXISTS "${_BD}")
            file(REMOVE_RECURSE "${_BD}")
        endif()
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy_directory "${_SRC_DIR}" "${_BD}"
                        RESULT_VARIABLE _r)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "[OpenSSL] Copy failed for ${CONFIG}")
        endif()
    endif()

    if(CONFIG STREQUAL "Debug")
        set(_dbg "--debug")
    else()
        set(_dbg "")
    endif()

    message(STATUS "[OpenSSL] Building ${CONFIG} (may take several minutes)...")

    if(WIN32)
        string(REPLACE "/" "\\" _BD_WIN "${_BD}")
        string(REPLACE "/" "\\" _VCVARS_WIN "${_VCVARS}")
        if(_dbg)
            set(_conf "perl Configure VC-WIN64A no-asm ${_dbg}")
        else()
            set(_conf "perl Configure VC-WIN64A no-asm")
        endif()
        set(_BAT "${CMAKE_BINARY_DIR}/_deps/openssl-${CONFIG}.bat")
        file(WRITE "${_BAT}"
            "@echo off\r\n"
            "set \"PATH=C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer;%PATH%\"\r\n"
            "call \"${_VCVARS_WIN}\" x64\r\n"
            "if errorlevel 1 exit /b 1\r\n"
            "cd /d \"${_BD_WIN}\"\r\n"
            "${_conf}\r\n"
            "if errorlevel 1 exit /b 1\r\n"
            "nmake build_libs\r\n"
            "if errorlevel 1 exit /b 1\r\n")
        execute_process(COMMAND cmd /c "${_BAT}"
                        RESULT_VARIABLE _r OUTPUT_VARIABLE _o ERROR_VARIABLE _e)
    else()
        set(_args ${_TARGET} no-asm no-shared)
        if(_dbg)
            list(APPEND _args ${_dbg})
        endif()
        execute_process(COMMAND ${_PERL} Configure ${_args}
                        WORKING_DIRECTORY "${_BD}" RESULT_VARIABLE _r ERROR_VARIABLE _e)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "[OpenSSL] ${CONFIG} configure failed:\n${_e}")
        endif()
        execute_process(COMMAND ${_MAKE} -j${_NPROC} build_libs
                        WORKING_DIRECTORY "${_BD}" RESULT_VARIABLE _r ERROR_VARIABLE _e)
    endif()
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "[OpenSSL] ${CONFIG} build failed:\n${_e}")
    endif()

    # Install libs
    file(MAKE_DIRECTORY "${OPENSSL_TP_LIB}")
    if(WIN32)
        set(_ext ".lib")
    else()
        set(_ext ".a")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${_BD}/libssl${_ext}"    "${OPENSSL_TP_LIB}/${SSL_DEST}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${_BD}/libcrypto${_ext}" "${OPENSSL_TP_LIB}/${CRYPTO_DEST}")

    # Install DLLs (Windows)
    if(WIN32 AND EXISTS "${_BD}/libssl-3-x64.dll")
        file(MAKE_DIRECTORY "${OPENSSL_TP_BIN}")
        if(CONFIG STREQUAL "Debug")
            set(_sdll "${_SSL_DLL_DBG}")
            set(_cdll "${_CRYPTO_DLL_DBG}")
        else()
            set(_sdll "${_SSL_DLL_REL}")
            set(_cdll "${_CRYPTO_DLL_REL}")
        endif()
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${_BD}/libssl-3-x64.dll"    "${OPENSSL_TP_BIN}/${_sdll}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${_BD}/libcrypto-3-x64.dll" "${OPENSSL_TP_BIN}/${_cdll}")
    endif()
    message(STATUS "[OpenSSL]   ${CONFIG} installed")
endfunction()

# Build each config if missing
if(NOT EXISTS "${OPENSSL_TP_LIB}/${_SSL_DBG}" OR NOT EXISTS "${OPENSSL_TP_LIB}/${_CRYPTO_DBG}")
    _openssl_build("Debug" "${_SSL_DBG}" "${_CRYPTO_DBG}")
endif()
if(NOT EXISTS "${OPENSSL_TP_LIB}/${_SSL_REL}" OR NOT EXISTS "${OPENSSL_TP_LIB}/${_CRYPTO_REL}")
    _openssl_build("Release" "${_SSL_REL}" "${_CRYPTO_REL}")
endif()

# Install headers (generated during Configure, use a build dir)
if(NOT EXISTS "${OPENSSL_TP_INCLUDE}/openssl/ssl.h")
    file(MAKE_DIRECTORY "${OPENSSL_TP_INCLUDE}")
    set(_hsrc "${CMAKE_BINARY_DIR}/_deps/openssl-build-Release")
    if(NOT EXISTS "${_hsrc}/include/openssl/ssl.h")
        set(_hsrc "${CMAKE_BINARY_DIR}/_deps/openssl-build-Debug")
    endif()
    if(NOT EXISTS "${_hsrc}/include/openssl/ssl.h")
        message(FATAL_ERROR "[OpenSSL] Generated headers not found")
    endif()
    file(COPY "${_hsrc}/include/openssl" DESTINATION "${OPENSSL_TP_INCLUDE}")
endif()

# Install license
if(EXISTS "${_SRC_DIR}/LICENSE.txt")
    file(COPY "${_SRC_DIR}/LICENSE.txt" DESTINATION "${OPENSSL_THIRD_PARTY}")
endif()

message(STATUS "[OpenSSL] Installed to ${OPENSSL_THIRD_PARTY}")
_openssl_create_targets()
