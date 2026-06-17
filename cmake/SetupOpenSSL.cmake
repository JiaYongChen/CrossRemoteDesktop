# SetupOpenSSL.cmake
# ──────────────────────────────────────────────────────────────────────────
# 直接从 third_party/openssl/ 预编译缓存读取（已提交 git）
# 缓存缺失时 FATAL_ERROR —— 开发者应通过 vcpkg 获取后缓存到 third_party/
#
# 输出: OpenSSL::SSL, OpenSSL::Crypto (imported, per-config)
#        OPENSSL_INCLUDE_DIR, OPENSSL_TP_BIN
# ──────────────────────────────────────────────────────────────────────────

set(_SSL_TP   "${CMAKE_SOURCE_DIR}/third_party/openssl")
set(_SSL_INC  "${_SSL_TP}/include")
set(_SSL_LIB  "${_SSL_TP}/lib/${PLATFORM_NAME}-${PLATFORM_ARCH}")
set(_SSL_BIN  "${_SSL_TP}/bin/${PLATFORM_NAME}-${PLATFORM_ARCH}")

# ── 平台特定文件名 ──────────────────────────────────────────────────────
if(WIN32)
    set(_SSL_REL_LIB     "libssl.lib")
    set(_SSL_DBG_LIB     "libsslD.lib")
    set(_CRYPTO_REL_LIB  "libcrypto.lib")
    set(_CRYPTO_DBG_LIB  "libcryptoD.lib")
    set(_SSL_REL_DLL     "libssl-3-x64.dll")
    set(_SSL_DBG_DLL     "libssl-3-x64D.dll")
    set(_CRYPTO_REL_DLL  "libcrypto-3-x64.dll")
    set(_CRYPTO_DBG_DLL  "libcrypto-3-x64D.dll")
else()
    set(_SSL_REL_LIB     "libssl.a")
    set(_SSL_DBG_LIB     "libsslD.a")
    set(_CRYPTO_REL_LIB  "libcrypto.a")
    set(_CRYPTO_DBG_LIB  "libcryptoD.a")
endif()

# ══════════════════════════════════════════════════════════════════════════
# 1) 检查预编译缓存是否完整
# ══════════════════════════════════════════════════════════════════════════
set(_MISSING "")
if(NOT EXISTS "${_SSL_INC}/openssl/ssl.h")
    list(APPEND _MISSING "  - 头文件: ${_SSL_INC}/openssl/ssl.h")
endif()
foreach(_f "${_SSL_REL_LIB}" "${_SSL_DBG_LIB}" "${_CRYPTO_REL_LIB}" "${_CRYPTO_DBG_LIB}")
    if(NOT EXISTS "${_SSL_LIB}/${_f}")
        list(APPEND _MISSING "  - 库文件: ${_SSL_LIB}/${_f}")
    endif()
endforeach()
if(WIN32)
    foreach(_f "${_SSL_REL_DLL}" "${_SSL_DBG_DLL}" "${_CRYPTO_REL_DLL}" "${_CRYPTO_DBG_DLL}")
        if(NOT EXISTS "${_SSL_BIN}/${_f}")
            list(APPEND _MISSING "  - DLL: ${_SSL_BIN}/${_f}")
        endif()
    endforeach()
endif()

if(_MISSING)
    string(JOIN "\n" _missing_list ${_MISSING})
    message(FATAL_ERROR
        "[OpenSSL] third_party/openssl/ 预编译缓存不完整，缺少以下文件:\n"
        "${_missing_list}\n\n"
        "  请通过 vcpkg 下载预编译包并缓存到 third_party/openssl/:\n"
        "    vcpkg install openssl:x64-windows\n"
        "  然后将产物按以下结构重组并提交 git:\n"
        "    third_party/openssl/\n"
        "      include/openssl/   ← 头文件\n"
        "      lib/Windows-x64/    ← 导入库 (libssl.lib, libcrypto.lib)\n"
        "      bin/Windows-x64/    ← DLL (libssl-3-x64.dll, libcrypto-3-x64.dll)")
endif()

# ══════════════════════════════════════════════════════════════════════════
# 2) 创建 imported targets
# ══════════════════════════════════════════════════════════════════════════
message(STATUS "[OpenSSL] Using ${_SSL_TP}")

set(OPENSSL_INCLUDE_DIR "${_SSL_INC}")
set(OPENSSL_TP_BIN      "${_SSL_BIN}")

if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OpenSSL::SSL PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES    "${_SSL_INC}"
        IMPORTED_LOCATION_DEBUG          "${_SSL_LIB}/${_SSL_DBG_LIB}"
        IMPORTED_LOCATION_RELEASE        "${_SSL_LIB}/${_SSL_REL_LIB}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${_SSL_LIB}/${_SSL_REL_LIB}"
        IMPORTED_LOCATION_MINSIZEREL     "${_SSL_LIB}/${_SSL_REL_LIB}"
    )
endif()
if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES    "${_SSL_INC}"
        IMPORTED_LOCATION_DEBUG          "${_SSL_LIB}/${_CRYPTO_DBG_LIB}"
        IMPORTED_LOCATION_RELEASE        "${_SSL_LIB}/${_CRYPTO_REL_LIB}"
        IMPORTED_LOCATION_RELWITHDEBINFO "${_SSL_LIB}/${_CRYPTO_REL_LIB}"
        IMPORTED_LOCATION_MINSIZEREL     "${_SSL_LIB}/${_CRYPTO_REL_LIB}"
    )
endif()
